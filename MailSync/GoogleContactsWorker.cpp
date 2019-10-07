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

string GOOGLE_SYNC_SOURCE = "gpeople";
string PERSON_FIELDS = "emailAddresses,genders,names,nicknames,phoneNumbers,photos,urls,birthdays,addresses,userDefined,relations,occupations,organizations";

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
    
    // Fetch all connections
    string peopleUrl = "https://people.googleapis.com/v1/people/me/connections?personFields=" + PERSON_FIELDS + "&pageSize=400";
    paginateGoogleCollection(peopleUrl, authorization, "gsynctoken-contacts-" + account->id(), ([&](json page) {
        for (const auto & conn : page["connections"]) {
            auto primaryName = conn.count("names") ? conn["names"][0]["displayName"].get<string>() : "";
            auto primaryEmail = conn.count("emailAddresses") ? conn["emailAddresses"][0]["value"].get<string>() : "";
            auto resourceName = conn["resourceName"].get<string>();

            auto local = store->find<Contact>(Query().equal("accountId", account->id()).equal("source", GOOGLE_SYNC_SOURCE).equal("id", resourceName));

            // handle deleted contact
            if (conn.count("deleted") && conn["deleted"].get<bool>() && local) {
                store->remove(local.get());
                continue;
            }
            
            // handle new contact
            if (!local) {
                local = make_shared<Contact>(resourceName, account->id(), primaryEmail, CONTACT_MAX_REFS, GOOGLE_SYNC_SOURCE);
            }
            
            // process changes
            local->setEmail(primaryEmail);
            local->setName(primaryName);
            local->setBookId(book->id());
            local->setInfo(conn);
            store->save(local.get());
            
            logger->info("{} ({})", primaryName, resourceName);
        }
    }));
    
    // Fetch all groups
    string groupsUrl = "https://people.googleapis.com/v1/contactGroups?pageSize=400";
    paginateGoogleCollection(groupsUrl, authorization, "gsynctoken-groups-" + account->id(), ([&](json page) {
        for (const auto & group : page["contactGroups"]) {
            auto name = group["name"].get<string>();
            auto type = group["groupType"].get<string>();
            auto resourceName = group["resourceName"].get<string>();
            auto memberCount = group.count("memberCount") ? group["memberCount"].get<int>() : 0;
            
            if (type == "SYSTEM_CONTACT_GROUP") {
                continue;
            }
            
            auto local = store->find<ContactGroup>(Query().equal("accountId", account->id()).equal("id", resourceName));
            if (!local) {
                local = make_shared<ContactGroup>(resourceName, account->id());
            }
            local->setName(name);
            local->setBookId(book->id());
            store->save(local.get());

            vector<string> members;

            if (memberCount > 0) {
                auto json = PerformJSONRequest(CreateJSONRequest("https://people.googleapis.com/v1/" + resourceName + "?maxMembers=" + to_string(memberCount), "GET", authorization));
                for (const auto & memberId : json["memberResourceNames"]) {
                    members.push_back(memberId.get<string>());
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
        auto url = urlRoot;
        if (nextPageToken != "") {
            url = url + "&pageToken=" + nextPageToken;
        }
        if (syncToken != "") {
            url = url + "&syncToken=" + syncToken;
        } else if (urlRoot.find("connections") != std::string::npos) {
            url = url + "&requestSyncToken=true";
        }
        
        auto json = PerformJSONRequest(CreateJSONRequest(url, "GET", authorization));
        
        if (json.count("nextSyncToken")) {
            nextSyncToken = json["nextSyncToken"].get<string>();
            logger->info(nextSyncToken);
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
