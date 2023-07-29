//
//  MetadataWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "GoogleContactsWorker.hpp"
#include "MailStore.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Message.hpp"
#include "Thread.hpp"
#include "Contact.hpp"
#include "ContactBook.hpp"
#include "ContactGroup.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

#include <string>
#include <curl/curl.h>

string PERSON_FIELDS        = "emailAddresses,genders,names,nicknames,phoneNumbers,urls,birthdays,addresses,userDefined,relations,occupations,organizations,photos";
string PERSON_UPDATE_FIELDS = "emailAddresses,genders,names,nicknames,phoneNumbers,urls,birthdays,addresses,userDefined,relations,occupations,organizations";
string GOOGLE_PEOPLE_ROOT = "https://people.googleapis.com/v1/";

GoogleContactsWorker::GoogleContactsWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger"))
{
    if (account->provider() != "gmail") {
        throw "GoogleContactsWorker created with non-gmail account";
    }
}

void GoogleContactsWorker::run() {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    
    // Create the contact book, which is how the client knows contact sync is enabled.
    shared_ptr<ContactBook> book = store->find<ContactBook>(Query().equal("accountId", account->id()));
    if (!book) {
        book = make_shared<ContactBook>(account->id() + "-default", account->id());
        book->setSource(GOOGLE_SYNC_SOURCE);
        store->save(book.get());
    }
    
    auto _contactsLocal = store->findAll<Contact>(Query().equal("accountId", account->id()).equal("source", GOOGLE_SYNC_SOURCE));
    std::unordered_map<string, shared_ptr<Contact>> contactsLocal;
    for (const auto c : _contactsLocal) {
        contactsLocal[c->googleResourceName()] = c;
    }

    // Fetch all connections
    
    string peopleUrl = GOOGLE_PEOPLE_ROOT + "people/me/connections?personFields=" + PERSON_FIELDS + "&pageSize=400";
    paginateGoogleCollection(peopleUrl, authorization, "gsynctoken-contacts-" + account->id(), ([&](json page) {
        for (const auto & conn : page["connections"]) {
            auto resourceName = conn["resourceName"].get<string>();

            shared_ptr<Contact> local = contactsLocal[resourceName];
            
            // handle deleted contact
            if (conn.count("deleted") && conn["deleted"].get<bool>() && local) {
                store->remove(local.get());
                continue;
            }
            
            // handle photo of contact, with no other data. Unclear why these are sent and it seems to send
            // them for a contact AFTER you delete a contact. Maybe telling you to unsync the photo?
            if (conn.count("photos") && conn.count("etag") && conn.count("resourceName") && conn.size() == 3) {
                continue;
            }
            
            // handle new contact
            if (!local) {
                local = make_shared<Contact>(MailUtils::idRandomlyGenerated(), account->id(), "", CONTACT_MAX_REFS, GOOGLE_SYNC_SOURCE);
                local->setGoogleResourceName(resourceName);
                local->setBookId(book->id());
                contactsLocal[resourceName] = local;
            }
            
            // process changes
            applyJSONToContact(local, conn);
            store->save(local.get());
        }
    }));
    
    // Fetch all groups
    auto groupsLocal = store->findAll<ContactGroup>(Query().equal("accountId", account->id()));
    
    paginateGoogleCollection(GOOGLE_PEOPLE_ROOT + "contactGroups?pageSize=400", authorization, "gsynctoken-groups-" + account->id(), ([&](json page) {
        for (const auto & group : page["contactGroups"]) {
            auto resourceName = group["resourceName"].get<string>();
            shared_ptr<ContactGroup> local = nullptr;
            for (auto g : groupsLocal) {
                if (g->googleResourceName() == resourceName) {
                    local = g;
                    break;
                }
            }

            // handle deleted group
            if (group.count("metadata") && group["metadata"].count("deleted") && group["metadata"]["deleted"].get<bool>() && local) {
                store->remove(local.get());
                continue;
            }

            // ignore Google's groups becaus they're mostly deprecated
            auto type = group["groupType"].get<string>();
            if (type == "SYSTEM_CONTACT_GROUP") {
                continue;
            }
            
            auto name = group.count("name") ? group["name"].get<string>() : "";
            auto memberCount = group.count("memberCount") ? group["memberCount"].get<int>() : 0;

            // handle created group
            if (local == nullptr) {
                local = make_shared<ContactGroup>(MailUtils::idRandomlyGenerated(), account->id());
                local->setGoogleResourceName(resourceName);
            }
            local->setName(name);
            local->setBookId(book->id());
            store->save(local.get());

            vector<string> members;

            // whenever a group changes, we re-sync it's membership list
            if (memberCount > 0) {
                auto json = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + resourceName + "?maxMembers=" + to_string(memberCount), "GET", authorization));
                for (const auto & memberResourceName : json["memberResourceNames"]) {
                    auto mrn = memberResourceName.get<string>();
                    auto c = contactsLocal[mrn];
                    if (c) {
                        members.push_back(c->id());
                    } else {
                        logger->warn("Google group references resourceName not found: {}", mrn);
                    }
                }
            }
            local->syncMembers(store, members);
        }
    }));
}

void GoogleContactsWorker::paginateGoogleCollection(string urlRoot, string authorization, string syncTokenKey, std::function<void(json)> yieldBlock) {
    string syncToken = store->getKeyValue(syncTokenKey);
    
    string nextPageToken = "";
    string nextSyncToken = "";
    
    while (nextPageToken != "END") {
        // Debounce 2sec on each request because Google has a 90 req. per second
        // limit per user on the contacts API and is fast enough we blow through it.
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        auto url = urlRoot;
        if (nextPageToken != "") {
            url = url + "&pageToken=" + nextPageToken;
        }
        if (syncToken != "") {
            url = url + "&syncToken=" + syncToken;
        } else if (urlRoot.find("connections") != std::string::npos) {
            url = url + "&requestSyncToken=true";
        }
        
        auto json = json::object();
        try {
            json = PerformJSONRequest(CreateJSONRequest(url, "GET", authorization));
        } catch (SyncException & ex) {
            if (ex.debuginfo.find("Sync token is expired") != string::npos) {
                store->saveKeyValue(syncTokenKey, "");
            }
            throw;
        }
        
        if (json.count("nextSyncToken")) {
            nextSyncToken = json["nextSyncToken"].get<string>();
        }
        if (json.count("nextPageToken")) {
            nextPageToken = json["nextPageToken"].get<string>();
        } else {
            nextPageToken = "END";
        }
        
        yieldBlock(json);
        
    }

    if (nextSyncToken != "") {
        store->saveKeyValue(syncTokenKey, nextSyncToken);
    }
}

void GoogleContactsWorker::upsertContactGroup(shared_ptr<ContactGroup> group) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    
    json resp;
    if (group->googleResourceName() != "") {
        auto data = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + group->googleResourceName(), "GET", authorization));
        data["name"] = group->name();
        json payload = {{"contactGroup", data}};
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + group->googleResourceName(), "PUT", authorization, payload.dump().c_str()));
    } else {
        json payload = {{"contactGroup", {{"name", group->name() }} }};
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + "contactGroups", "POST", authorization, payload.dump().c_str()));
    }
    logger->info("Response {}", resp.dump());

    group->setGoogleResourceName(resp["resourceName"].get<string>());
    store->save(group.get());
}

void GoogleContactsWorker::deleteContactGroup(string groupResourceName) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    auto resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + groupResourceName, "DELETE", authorization));
    logger->warn("respresp: {}", resp.dump());
}

void GoogleContactsWorker::updateContactGroupMembership(shared_ptr<ContactGroup> group, vector<shared_ptr<Contact>> contacts, string direction) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    json payload = json::object();
    
    vector<string> resourceNames;
    for (auto c : contacts) {
        resourceNames.push_back(c->googleResourceName());
    }
    if (direction == "add") {
        payload["resourceNamesToAdd"] = resourceNames;
    } else {
        payload["resourceNamesToRemove"] = resourceNames;
    };
    PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + group->googleResourceName() + "/members:modify", "POST", authorization, payload.dump().c_str()));
}

void GoogleContactsWorker::deleteContact(shared_ptr<Contact> contact) {
    if (contact->source() != GOOGLE_SYNC_SOURCE) {
        return;
    }

    if (contact->googleResourceName() == "") {
        return;
    }
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    auto resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + contact->googleResourceName() + ":deleteContact", "DELETE", authorization));
    store->remove(contact.get());
}

void GoogleContactsWorker::upsertContact(shared_ptr<Contact> contact) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    string authorization = "Bearer " + parts.accessToken;
    string body = contact->info().dump();
    
    logger->info("Upserting contact with data: {}", body);
    json resp;
    if (contact->googleResourceName() != "") {
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + contact->googleResourceName() + ":updateContact?updatePersonFields=" + PERSON_UPDATE_FIELDS, "PATCH", authorization, body.c_str()));
    } else {
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + "people:createContact", "POST", authorization, body.c_str()));
        contact->setGoogleResourceName(resp["resourceName"].get<string>());
    }
    logger->info("Upserting contact complete: {}", resp.dump());
    applyJSONToContact(contact, resp);
    store->save(contact.get());
}

void GoogleContactsWorker::applyJSONToContact(shared_ptr<Contact> local, const json & conn) {
    auto primaryName = (conn.count("names") && conn["names"][0].count("displayName"))
        ? conn["names"][0]["displayName"].get<string>() : "";
    auto primaryEmail = (conn.count("emailAddresses") && conn["emailAddresses"][0].count("value"))
        ? conn["emailAddresses"][0]["value"].get<string>() : "";

    local->setEmail(primaryEmail);
    local->setName(primaryName);
    local->setInfo(conn);
}
