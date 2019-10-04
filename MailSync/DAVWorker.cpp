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

#include "DAVWorker.hpp"
#include "ContactGroup.hpp"
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
#include <belr/belr.h>
#include <belcard/belcard.hpp>

#define CARDDAV_SYNC_SOURCE "carddav"
#define XKIND "X-ADDRESSBOOKSERVER-KIND"
#define XMEMBERS "X-ADDRESSBOOKSERVER-MEMBER"

struct EventResult {
    std::string icsHref;
    std::string etag;
};

typedef string ETAG;

using namespace::belr;
using namespace::belcard;

string firstPropVal(json attrs, string key, string fallback = "") {
    if (!attrs.count(key)) return fallback;
    if (!attrs[key].size()) return fallback;
    return attrs[key][0].get<string>();
}

string replacePath(string url, string path) {
    unsigned long hostStart = url.find("://");
    unsigned long hostEnd = 0;
    if (hostStart == string::npos) {
        hostEnd = url.find("/");
    } else {
        hostEnd = url.substr(hostStart + 3).find("/");
        if (hostEnd != string::npos) {
            hostEnd += hostStart + 3;
        }
    }
    if (path.find("/") != 0) {
        path = "/" + path;
    }
    return url.substr(0, hostEnd) + path;
}

DAVWorker::DAVWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger"))
{
    // For now, we assume Gmail
    if (account->provider() == "gmail") {
        calHost = "apidata.googleusercontent.com";
        calPrincipal = "/caldav/v2/" + account->emailAddress();
        cardHost = "";
        cardPrincipal = "";
    }
    if (account->IMAPHost().find("mail.me.com") != string::npos) {
        calHost = "";
        calPrincipal = "";
        cardHost = "contacts.icloud.com";
        cardPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.aol.com") != string::npos) {
        calHost = "caldav.aol.com";
        calPrincipal = "discover";
        cardHost = "carddav.aol.com";
        cardPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.gmx.com") != string::npos || account->IMAPHost().find("imap.gmx.net") != string::npos) {
        calHost = "caldav.gmx.net";
        calPrincipal = "discover";
        cardHost = "carddav.gmx.net";
        cardPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.mail.ru") != string::npos) {
        calHost = "calendar.mail.ru";
        calPrincipal = "discover";
        cardHost = "";
        cardPrincipal = "";
    }
    if (account->IMAPHost().find("imap.yandex.com") != string::npos) {
        calHost = "yandex.ru";
        calPrincipal = "discover";
        cardHost = "yandex.ru";
        cardPrincipal = "discover";
    }
    if (account->IMAPHost().find("securemail.a1.net") != string::npos) {
        calHost = "caldav.a1.net";
        calPrincipal = "discover";
        cardHost = "carddav.a1.net";
        cardPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.zoho.com") != string::npos) {
        calHost = "calendar.zoho.com";
        calPrincipal = "discover";
        cardHost = "contacts.zoho.com";
        cardPrincipal = "discover";
    }
    
    logger->info("CalDav config: {} {}", calHost, calPrincipal);
    logger->info("CardDav config: {} {}", cardHost, cardPrincipal);
    
    // Initialize libxml
    xmlInitParser();
}

void DAVWorker::run() {
    runContacts();
    runCalendars();
}

void DAVWorker::runContacts() {
    if (cardHost == "") {
        return;
    }
    if (cardPrincipal == "discover") {
        // Fetch the list of calendars from the principal URL
        auto principalDoc = performXMLRequest(cardHost + "/", "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><A:propfind xmlns:A=\"DAV:\"><A:prop><A:current-user-principal/><A:principal-URL/><A:resourcetype/></A:prop></A:propfind>");
        cardPrincipal = principalDoc->nodeContentAtXPath("//D:current-user-principal/D:href/text()");
    }
    
    // Fetch the address book home set URL
    auto abSetDoc = performXMLRequest(cardHost + cardPrincipal, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><A:propfind xmlns:A=\"DAV:\"><A:prop><A:displayname/><A:resourcetype/><B:addressbook-home-set xmlns:B=\"urn:ietf:params:xml:ns:carddav\"/></A:prop></A:propfind>");
    auto abSetURL = abSetDoc->nodeContentAtXPath("//carddav:addressbook-home-set/D:href/text()");
    if (abSetURL.find("http") == string::npos) {
        abSetURL = cardHost + abSetURL;
    }
    
    // Hit the home address book set to retrieve the individual address books
    auto abSetContentsDoc = performXMLRequest(abSetURL, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\"><d:prop><d:resourcetype /></d:prop></d:propfind>");
                                              
    // Iterate over the address books and run a sync on each one
    abSetContentsDoc->evaluateXPath("//D:response[.//carddav:addressbook]", ([&](xmlNodePtr node) {
        string abHREF = abSetContentsDoc->nodeContentAtXPath(".//D:href/text()", node);
        string abURL = (abHREF.find("http") == string::npos) ? replacePath(abSetURL, abHREF) : abHREF;
        runForAddressBook(MailUtils::idForCalendar(account->id(), abHREF), abURL);
    }));
}

void DAVWorker::runForAddressBook(string abID, string abURL) {
    map<ETAG, string> remote {};
    {
        auto etagsDoc = performXMLRequest(abURL, "REPORT", "<c:addressbook-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /></d:prop></c:addressbook-query>");

        etagsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            auto etag = etagsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
            auto href = etagsDoc->nodeContentAtXPath(".//D:href/text()", node);
            remote[string(etag.c_str())] = string(href.c_str());
        }));
    }
    

    // Because etags change when the event content changes, we only need to ADD new events
    // and DELETE missing events. To do this we query just the index for IDs and go from there.
    map<ETAG, bool> local {};
    {
        SQLite::Statement findEtags(store->db(), "SELECT etag FROM Contact WHERE remoteCollectionId = ?");
        findEtags.bind(1, abID);
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
    if (deleted.size() || needed.size()) {
        logger->info("{}", abURL);
        logger->info("  remote: {} etags", remote.size());
        logger->info("   local: {} etags", local.size());
        logger->info(" deleted: {}", deleted.size());
        logger->info("  needed: {}", needed.size());
    }
    
    auto deletionChunks = MailUtils::chunksOfVector(deleted, 100);
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 100)) {
        string payload = "";
        for (auto & href : chunk) {
            payload += "<d:href>" + href + "</d:href>";
        }
        
        // Fetch the data
        auto abDoc = performXMLRequest(abURL, "REPORT", "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /><c:address-data /></d:prop>" + payload + "</c:addressbook-multiget>");
        
        // Insert the event objects and remove deleted events within the same transaction.
        // Most of the time, this results in an event being replaced within a single transaction.
        {
            MailStoreTransaction transaction {store};
            
            if (!deletionChunks.empty()) {
                for (auto & deletionChunk : deletionChunks) {
                    // Delete the contacts
                    vector<string> possibleGroupIds {};
                    auto deletedItem = store->findAll<Contact>(Query().equal("remoteCollectionId", abID).equal("etag", deletionChunk));
                    for (auto & e : deletedItem) {
                        possibleGroupIds.push_back(e->id());
                        store->remove(e.get());
                    }
                    
                    // Because contact groups are built from contacts, we might have a group matching the contact's ID.
                    // In that case, retrieve and delete the groups too. (The relations are deleted in afterRemove).
                    auto deletedGroups = store->findAll<ContactGroup>(Query().equal("id", possibleGroupIds));
                    for (auto & g : deletedGroups) {
                        store->remove(g.get());
                    }
                }
                deletionChunks.clear();
            }
            
            abDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                auto etag = abDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                if (local.count(etag) == 0) {
                    auto href = abDoc->nodeContentAtXPath(".//D:href/text()", node);
                    auto vcard = abDoc->nodeContentAtXPath(".//carddav:address-data/text()", node);
                    if (vcard != "") {
                        local[etag] = true;
                        
                        auto belCard = BelCardParser::getInstance()->parseOne(vcard);
                        if (belCard == NULL) {
                            return;
                        }
                        string id = belCard->getUniqueId()->getValue();
                        if (id == "") id = MailUtils::idForCalendar(account->id(), href);
                        auto emailEntry = belCard->getEmails().front();
                        string email = emailEntry == NULL ? "" : emailEntry->getValue();
                        string name = belCard->getFullName()->getValue();
                        if (name == "") name = belCard->getName()->getValue();
                        
                        json basicJSON = {{"id", id}, {"name", name}, {"email", email}, {"etag", etag}, {"rci", abID}, {"info", {{"vcf", vcard}}}, {"refs", CONTACT_MAX_REFS}};
                        auto contact = make_shared<Contact>(account->id(), email, basicJSON, CARDDAV_SYNC_SOURCE);
                        
                        bool isGroup = false;
                        for (auto prop : belCard->getExtendedProperties()) {
                            if (prop->getName() == "X-ADDRESSBOOKSERVER-KIND" && prop->getValue() == "group") {
                                isGroup = true;
                                break;
                            }
                        }
                        
                        if (isGroup) {
                            contact->setHidden(true);
                            rebuildContactGroup(contact);
                        }

                        store->save(contact.get());
                    
                    } else {
                        logger->info("Received addressbook entry {} with an empty body", etag);
                    }
                }
            }));
            
            transaction.commit();
        }
    }
}

void DAVWorker::rebuildContactGroup(shared_ptr<Contact> contact) {
    // Support iCloud's "Contact Groups are just Contacts with a member list field" idea
    // by hiding the contact (so it's still synced via etag) and creating a group for it.
    // Note that we have to tear this down when we unsync the contact.
    auto group = store->find<ContactGroup>(Query().equal("id", contact->id()));
    if (!group) {
        group = make_shared<ContactGroup>(contact->id(), account->id());
    }
    group->setName(contact->name());
    store->save(group.get());
    
    SQLite::Statement removeMembers(store->db(), "DELETE FROM ContactContactGroup WHERE id = ?");
    removeMembers.bind(1, group->id());
    removeMembers.exec();

    SQLite::Statement insertMembers(store->db(), "INSERT OR IGNORE INTO ContactContactGroup (id, value) VALUES (?,?)");
    
    string vcard = contact->info()["vcf"].get<string>();
    shared_ptr<BelCard> belCard = BelCardParser::getInstance()->parseOne(vcard);
    if (belCard == NULL) {
        return;
    }
    
    for (const auto & prop : belCard->getExtendedProperties()) {
        if (prop->getName()!= "X-ADDRESSBOOKSERVER-MEMBER") continue;
        string mid = prop->getValue();
        if (mid.find("urn:uuid:") == 0) {
            mid = mid.substr(9);
        }
        insertMembers.bind(1, mid);
        insertMembers.bind(2, group->id());
        insertMembers.exec();
        insertMembers.reset();
    }
}

void DAVWorker::runCalendars() {
    if (calHost == "") {
        return;
    }

    // Fetch the list of calendars from the principal URL
    auto calendarSetDoc = performXMLRequest(calHost + calPrincipal, "PROPFIND", "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:resourcetype /><d:displayname /><cs:getctag /><c:supported-calendar-component-set /></d:prop></d:propfind>");

    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    
    // Iterate over the calendars that expose "VEVENT" components
    calendarSetDoc->evaluateXPath("//D:response[./D:propstat/D:prop/caldav:supported-calendar-component-set/caldav:comp[@name='VEVENT']]", ([&](xmlNodePtr node) {
        // Make a few xpath queries relative to the "D:response" calendar node (using "./")
        // to retrieve the attributes we're interested in.
        auto name = calendarSetDoc->nodeContentAtXPath(".//D:displayname/text()", node);
        auto path = calendarSetDoc->nodeContentAtXPath(".//D:href/text()", node);
        auto id = MailUtils::idForCalendar(account->id(), path);
        
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
        runForCalendar(id, name, calHost + path);
    }));
}

void DAVWorker::runForCalendar(string calendarId, string name, string url) {
    map<ETAG, string> remote {};
    {
        // Request the ETAG value of every event in the calendar. We should compare these
        // values against a set in the database. Any event we don't have should be added
        // and any event in the database absent from the response should be deleted.
        auto eventEtagsDoc = performXMLRequest(url, "REPORT", "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /></d:prop></c:calendar-query>");

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
    if (deleted.size() || needed.size()) {
        logger->info("{}", url);
        logger->info("  remote: {} etags", remote.size());
        logger->info("   local: {} etags", local.size());
        logger->info(" deleted: {}", deleted.size());
        logger->info("  needed: {}", needed.size());
    }
    
    auto deletionChunks = MailUtils::chunksOfVector(deleted, 100);
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 50)) {
        string payload = "";
        for (auto & icsHref : chunk) {
            payload += "<D:href>" + icsHref + "</D:href>";
        }
        
        // Fetch the data
        auto icsDoc = performXMLRequest(url, "REPORT", "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /><c:calendar-data /></d:prop>" + payload + "</c:calendar-multiget>");
        
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

shared_ptr<DavXML> DAVWorker::performXMLRequest(string _url, string method, string payload) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
    headers = curl_slist_append(headers, "Depth: 1");

    if (account->refreshToken() != "") {
        auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        string authorization = "Authorization: Bearer " + parts.accessToken;
        headers = curl_slist_append(headers, authorization.c_str());
    } else {
        string plain = account->IMAPUsername() + ":" + account->IMAPPassword();
        string encoded = MailUtils::toBase64(plain.c_str(), strlen(plain.c_str()));
        string authorization = "Authorization: Basic " + encoded;
        headers = curl_slist_append(headers, authorization.c_str());
    }
    
    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    string result = PerformRequest(curl_handle);
    return make_shared<DavXML>(result, url);
}

