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

#include "CalendarWorker.hpp"
#include "MailStore.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Message.hpp"
#include "Thread.hpp"
#include "Contact.hpp"
#include "SyncException.hpp"
#include "Event.hpp"
#include "Calendar.hpp"
#include "NetworkRequestUtils.hpp"

#include <string>
#include <curl/curl.h>

struct EventResult {
    std::string icsHref;
    std::string etag;
};

typedef string ETAG;


CalendarWorker::CalendarWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger"))
{
    // For now, we assume Gmail
    if (account->provider() == "gmail") {
        server = "https://apidata.googleusercontent.com";
        principalPath = "/caldav/v2/" + account->emailAddress();
    }
    // Initialize libxml
    xmlInitParser();
}

void CalendarWorker::run() {
    if (server == "" || principalPath == "") {
        return;
    }

    // Fetch the list of calendars from the principal URL
    auto calendarSetDoc = performXMLRequest(principalPath, "PROPFIND", "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:resourcetype /><d:displayname /><cs:getctag /><c:supported-calendar-component-set /></d:prop></d:propfind>");

    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    
    // Iterate over the calendars that expose "VEVENT" components
    calendarSetDoc->evaluateXPath("//D:response[./D:propstat/D:prop/caldav:supported-calendar-component-set/caldav:comp[@name='VEVENT']]", ([&](xmlNodePtr node) {
        // Make a few xpath queries relative to the "D:response" calendar node (using "./")
        // to retrieve the attributes we're interested in.
        auto name = calendarSetDoc->nodeContentAtXPath(".//D:displayname/text()", node);
        auto path = calendarSetDoc->nodeContentAtXPath(".//D:href/text()", node);
        auto id = MailUtils::idForCalendar(account->id(), path);
        fprintf(stdout, "%s\n", name.c_str());
        fprintf(stdout, "%s\n", path.c_str());
        
        // upsert the Calendar object
        {
            if (local[id]) {
                if (local[id]->name() != name) {
                    local[id]->setName(name);
                    store->save(local[id].get());
                }
            } else {
                Calendar cal = Calendar(id, account->id());
                cal.setPath(path);
                cal.setName(name);
                store->save(&cal);
            }
        }
        
        // sync
        runForCalendar(id, name, path);
    }));
}

void CalendarWorker::runForCalendar(string calendarId, string name, string path) {
    map<ETAG, string> remote {};
    {
        // Request the ETAG value of every event in the calendar. We should compare these
        // values against a set in the database. Any event we don't have should be added
        // and any event in the database absent from the response should be deleted.
        auto eventEtagsDoc = performXMLRequest(path, "REPORT", "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /></d:prop><c:filter></c:filter></c:calendar-query>");

        eventEtagsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            auto etag = eventEtagsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
            auto icsHref = eventEtagsDoc->nodeContentAtXPath(".//D:href/text()", node);
            remote[string(etag.c_str())] = string(icsHref.c_str());
        }));
    }
    

    // Because etags change when the event content changes, we only need to ADD new events
    // and DELETE missing events. To do this we query just the index for IDs and go from there.
    map<ETAG, bool> local {};
    {
        SQLite::Statement findEtags(store->db(), "SELECT etag FROM Event WHERE calendarId = ?");
        findEtags.bind(1, calendarId);
        while (findEtags.executeStep()) {
            local[findEtags.getColumn("etag")] = true;
        }
    }

    // identify new and deleted events
    vector<ETAG> deleted {};
    vector<string> needed {};
    for (auto & pair : remote) {
        if (local.count(pair.first)) continue;
        needed.push_back(pair.second);
    }
    for (auto & pair : local) {
        if (remote.count(pair.first)) continue;
        deleted.push_back(pair.first);
    }
    
    // request the last (newest) events first. Technically this should be unordered
    // by now, but it appears to work for me.
    std::reverse(needed.begin(), needed.end());
    
    logger->info("{}", path);
    logger->info("  remote: {} etags", remote.size());
    logger->info("   local: {} etags", local.size());
    logger->info(" deleted: {}", deleted.size());
    logger->info("  needed: {}", needed.size());
    
    auto deletionChunks = MailUtils::chunksOfVector(deleted, 100);
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 50)) {
        string payload = "";
        for (auto & icsHref : chunk) {
            payload += "<D:href>" + icsHref + "</D:href>";
        }
        
        // Fetch the data
        auto icsDoc = performXMLRequest(path, "REPORT", "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /><c:calendar-data /></d:prop>" + payload + "</c:calendar-multiget>");
        
        // Insert the event objects and remove deleted events within the same transaction.
        // Most of the time, this results in an event being replaced within a single transaction.
        {
            MailStoreTransaction transaction {store};
            
            icsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                auto etag = icsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                if (local.count(etag) == 0) {
                    auto icsData = icsDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);
                    if (icsData != "") {
                        local[etag] = true;
                        ICalendar cal(icsData);
                        auto icsEvent = cal.Events.front();
                        if (!icsEvent->DtStart.IsEmpty() && !icsEvent->DtEnd.IsEmpty()) {
                            auto event = Event(etag, account->id(), calendarId, icsData, icsEvent);
                            store->save(&event);
                        } else {
                            logger->info("Received calendar event but it has no start/end time?\n\n{}\n\n", icsData);
                        }
                    } else {
                        logger->info("Received calendar event {} with an empty body", etag);
                    }
                } else {
                    // we didn't ask for this, or we already received it in this session
                }
            }));
            
            if (!deletionChunks.empty()) {
                auto deletionChunk = deletionChunks.back();
                deletionChunks.pop_back();
                auto deletionEvents = store->findAll<Event>(Query().equal("calendarId", calendarId).equal("etag", deletionChunk));
                for (auto & e : deletionEvents) {
                    store->remove(e.get());
                }
            }
            transaction.commit();
        }
    }

    // Delete any remaining events
    {
        MailStoreTransaction transaction {store};
        for (auto & deletionChunk : deletionChunks) {
            auto deletionEvents = store->findAll<Event>(Query().equal("calendarId", calendarId).equal("etag", deletionChunk));
            for (auto & e : deletionEvents) {
                store->remove(e.get());
            }
        }
        transaction.commit();
    }
}

shared_ptr<DavXML> CalendarWorker::performXMLRequest(string path, string method, string payload) {
    string url = server + path;
    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
    headers = curl_slist_append(headers, "Depth: 1");

    if (account->refreshToken() != "") {
        auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        string authorization = "Authorization: Bearer " + parts.accessToken;
        headers = curl_slist_append(headers, authorization.c_str());
    } else {
        // to use basic auth:
        // url.replace(url.find("://"), 3, "://" + username + ":" + password + "@");
    }
    
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    string result = PerformRequest(curl_handle);
    return make_shared<DavXML>(result, url);
}

