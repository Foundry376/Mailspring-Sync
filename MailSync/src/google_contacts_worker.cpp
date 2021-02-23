#include "mailsync/google_contacts_worker.hpp"
#include "mailsync/mail_store.hpp"
#include "mailsync/mail_store_transaction.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/message.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/contact.hpp"
#include "mailsync/models/contact_book.hpp"
#include "mailsync/models/contact_group.hpp"
#include "mailsync/sync_exception.hpp"
#include "mailsync/network_request_utils.hpp"

#include "nlohmann/json.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <curl/curl.h>

std::string PERSON_FIELDS        = "emailAddresses,genders,names,nicknames,phoneNumbers,urls,birthdays,addresses,userDefined,relations,occupations,organizations,photos";
std::string PERSON_UPDATE_FIELDS = "emailAddresses,genders,names,nicknames,phoneNumbers,urls,birthdays,addresses,userDefined,relations,occupations,organizations";
std::string GOOGLE_PEOPLE_ROOT = "https://people.googleapis.com/v1/";

GoogleContactsWorker::GoogleContactsWorker(std::shared_ptr<Account> account) :
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
    std::string authorization = "Bearer " + parts.accessToken;

    // Create the contact book, which is how the client knows contact sync is enabled.
    std::shared_ptr<ContactBook> book = store->find<ContactBook>(Query().equal("accountId", account->id()));
    if (!book) {
        book = std::make_shared<ContactBook>(account->id() + "-default", account->id());
        book->setSource(GOOGLE_SYNC_SOURCE);
        store->save(book.get());
    }

    auto _contactsLocal = store->findAll<Contact>(Query().equal("accountId", account->id()).equal("source", GOOGLE_SYNC_SOURCE));
    std::unordered_map<std::string, std::shared_ptr<Contact>> contactsLocal;
    for (const auto c : _contactsLocal) {
        contactsLocal[c->googleResourceName()] = c;
    }

    // Fetch all connections

    std::string peopleUrl = GOOGLE_PEOPLE_ROOT + "people/me/connections?personFields=" + PERSON_FIELDS + "&pageSize=400";
    paginateGoogleCollection(peopleUrl, authorization, "gsynctoken-contacts-" + account->id(), ([&](nlohmann::json page) {
        for (const auto & conn : page["connections"]) {
            auto resourceName = conn["resourceName"].get<std::string>();

            std::shared_ptr<Contact> local = contactsLocal[resourceName];

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
                local = std::make_shared<Contact>(MailUtils::idRandomlyGenerated(), account->id(), "", CONTACT_MAX_REFS, GOOGLE_SYNC_SOURCE);
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

    paginateGoogleCollection(GOOGLE_PEOPLE_ROOT + "contactGroups?pageSize=400", authorization, "gsynctoken-groups-" + account->id(), ([&](nlohmann::json page) {
        for (const auto & group : page["contactGroups"]) {
            auto resourceName = group["resourceName"].get<std::string>();
            std::shared_ptr<ContactGroup> local = nullptr;
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
            auto type = group["groupType"].get<std::string>();
            if (type == "SYSTEM_CONTACT_GROUP") {
                continue;
            }

            auto name = group.count("name") ? group["name"].get<std::string>() : "";
            auto memberCount = group.count("memberCount") ? group["memberCount"].get<int>() : 0;

            // handle created group
            if (local == nullptr) {
                local = std::make_shared<ContactGroup>(MailUtils::idRandomlyGenerated(), account->id());
                local->setGoogleResourceName(resourceName);
            }
            local->setName(name);
            local->setBookId(book->id());
            store->save(local.get());

            std::vector<std::string> members;

            // whenever a group changes, we re-sync it's membership list
            if (memberCount > 0) {
                auto json = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + resourceName + "?maxMembers=" + to_string(memberCount), "GET", authorization));
                for (const auto & memberResourceName : json["memberResourceNames"]) {
                    auto mrn = memberResourceName.get<std::string>();
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

void GoogleContactsWorker::paginateGoogleCollection(std::string urlRoot, std::string authorization, std::string syncTokenKey, std::function<void(nlohmann::json)> yieldBlock) {
    std::string syncToken = store->getKeyValue(syncTokenKey);

    std::string nextPageToken = "";
    std::string nextSyncToken = "";

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

        auto json = nlohmann::json::object();
        try {
            json = PerformJSONRequest(CreateJSONRequest(url, "GET", authorization));
        } catch (SyncException & ex) {
            if (ex.debuginfo.find("Sync token is expired") != std::string::npos) {
                store->saveKeyValue(syncTokenKey, "");
            }
            throw;
        }

        if (json.count("nextSyncToken")) {
            nextSyncToken = json["nextSyncToken"].get<std::string>();
        }
        if (json.count("nextPageToken")) {
            nextPageToken = json["nextPageToken"].get<std::string>();
        } else {
            nextPageToken = "END";
        }

        yieldBlock(json);

    }

    if (nextSyncToken != "") {
        store->saveKeyValue(syncTokenKey, nextSyncToken);
    }
}

void GoogleContactsWorker::upsertContactGroup(std::shared_ptr<ContactGroup> group) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    std::string authorization = "Bearer " + parts.accessToken;

    nlohmann::json resp;
    if (group->googleResourceName() != "") {
        auto data = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + group->googleResourceName(), "GET", authorization));
        data["name"] = group->name();
        nlohmann::json payload = {{"contactGroup", data}};
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + group->googleResourceName(), "PUT", authorization, payload.dump().c_str()));
    } else {
        nlohmann::json payload = {{"contactGroup", {{"name", group->name() }} }};
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + "contactGroups", "POST", authorization, payload.dump().c_str()));
    }
    logger->info("Response {}", resp.dump());

    group->setGoogleResourceName(resp["resourceName"].get<std::string>());
    store->save(group.get());
}

void GoogleContactsWorker::deleteContactGroup(std::string groupResourceName) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    std::string authorization = "Bearer " + parts.accessToken;
    auto resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + groupResourceName, "DELETE", authorization));
    logger->warn("respresp: {}", resp.dump());
}

void GoogleContactsWorker::updateContactGroupMembership(std::shared_ptr<ContactGroup> group, std::vector<std::shared_ptr<Contact>> contacts, std::string direction) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    std::string authorization = "Bearer " + parts.accessToken;
    nlohmann::json payload = nlohmann::json::object();

    std::vector<std::string> resourceNames;
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

void GoogleContactsWorker::deleteContact(std::shared_ptr<Contact> contact) {
    if (contact->source() != GOOGLE_SYNC_SOURCE) {
        return;
    }

    if (contact->googleResourceName() == "") {
        return;
    }
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    std::string authorization = "Bearer " + parts.accessToken;
    auto resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + contact->googleResourceName() + ":deleteContact", "DELETE", authorization));
    store->remove(contact.get());
}

void GoogleContactsWorker::upsertContact(std::shared_ptr<Contact> contact) {
    auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
    std::string authorization = "Bearer " + parts.accessToken;
    std::string body = contact->info().dump();

    logger->info("Upserting contact with data: {}", body);
    nlohmann::json resp;
    if (contact->googleResourceName() != "") {
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + contact->googleResourceName() + ":updateContact?updatePersonFields=" + PERSON_UPDATE_FIELDS, "PATCH", authorization, body.c_str()));
    } else {
        resp = PerformJSONRequest(CreateJSONRequest(GOOGLE_PEOPLE_ROOT + "people:createContact", "POST", authorization, body.c_str()));
        contact->setGoogleResourceName(resp["resourceName"].get<std::string>());
    }
    logger->info("Upserting contact complete: {}", resp.dump());
    applyJSONToContact(contact, resp);
    store->save(contact.get());
}

void GoogleContactsWorker::applyJSONToContact(std::shared_ptr<Contact> local, const nlohmann::json & conn) {
    auto primaryName = conn.count("names") ? conn["names"][0]["displayName"].get<std::string>() : "";
    auto primaryEmail = conn.count("emailAddresses") ? conn["emailAddresses"][0]["value"].get<std::string>() : "";
    local->setEmail(primaryEmail);
    local->setName(primaryName);
    local->setInfo(conn);
}
