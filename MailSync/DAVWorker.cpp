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
#include "DAVUtils.hpp"
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
    auto ab = resolveAddressBook();
    if (ab.id != "") {
        runForAddressBook(ab);
    }
}

AddressBookResult DAVWorker::resolveAddressBook() {
    if (cardHost == "") {
        return AddressBookResult();
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
    // TODO: Pick the primary one somehow!
    AddressBookResult result { "", "" };
    
    abSetContentsDoc->evaluateXPath("//D:response[.//carddav:addressbook]", ([&](xmlNodePtr node) {
        string abHREF = abSetContentsDoc->nodeContentAtXPath(".//D:href/text()", node);
        string abURL = (abHREF.find("http") == string::npos) ? replacePath(abSetURL, abHREF) : abHREF;
        result.id = MailUtils::idForCalendar(account->id(), abHREF);
        result.url = abURL;
    }));
    
    return result;
}

void DAVWorker::writeAndResyncContact(shared_ptr<Contact> contact) {
    auto ab = resolveAddressBook();
    
    if (ab.id == "") {
        logger->warn("No address book found.");
        return;
    }

    if (contact->info().count("href") == 0) {
        logger->warn("No href in info.");
        return;
    }
    string vcf = contact->info()["vcf"].get<string>();
    string href = contact->info()["href"].get<string>();
    
    // write the card
    performVCardRequest(replacePath(ab.url, href), "PUT", vcf, contact->etag());
    
    // read the card back to ingest server-side changes and the new etag
    // IMPORTANT: We receive a new copy of this contact with possibly new data.
    // DO NOT SAVE the one in the outer scope!
    auto abDoc = performXMLRequest(ab.url, "REPORT", "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /><c:address-data /></d:prop><d:href>"+href+"</d:href></c:addressbook-multiget>");
    abDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
        bool isGroup = false;
        auto contact = ingestAddressDataNode(abDoc, node, isGroup);
        if (!contact) return;
        contact->setRemoteCollectionId(ab.id);
        if (isGroup) {
            rebuildContactGroup(contact);
        }
        store->save(contact.get());
    }));
}

void DAVWorker::deleteContact(shared_ptr<Contact> contact) {
    auto ab = resolveAddressBook();
    string vcf = contact->info()["vcf"].get<string>();
    
    if (ab.id == "") {
        logger->warn("No address book found.");
        return;
    }

    if (contact->info().count("href") == 0) {
        logger->warn("No href in info.");
        return;
    }

    string href = contact->info()["href"].get<string>();
    
    performVCardRequest(replacePath(ab.url, href), "DELETE", "", contact->etag());
    store->remove(contact.get());
}

void DAVWorker::runForAddressBook(AddressBookResult ab) {
    map<ETAG, string> remote {};
    {
        auto etagsDoc = performXMLRequest(ab.url, "REPORT", "<c:addressbook-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /></d:prop></c:addressbook-query>");

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
        findEtags.bind(1, ab.id);
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
        logger->info("{}", ab.url);
        logger->info("  remote: {} etags", remote.size());
        logger->info("   local: {} etags", local.size());
        logger->info(" deleted: {}", deleted.size());
        logger->info("  needed: {}", needed.size());
    }
    
    auto deletionChunks = MailUtils::chunksOfVector(deleted, 100);
    vector<shared_ptr<Contact>> updatedGroups;
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 50)) {
        string payload = "";
        for (auto & href : chunk) {
            payload += "<d:href>" + href + "</d:href>";
        }
        
        // Fetch the data
        auto abDoc = performXMLRequest(ab.url, "REPORT", "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /><c:address-data /></d:prop>" + payload + "</c:addressbook-multiget>");
        
        // Insert the event objects and remove deleted events within the same transaction.
        // Most of the time, this results in an event being replaced within a single transaction.
        {
            MailStoreTransaction transaction {store};
            
            if (!deletionChunks.empty()) {
                for (auto & deletionChunk : deletionChunks) {
                    // Delete the contacts
                    vector<string> possibleGroupIds {};
                    auto deletedItem = store->findAll<Contact>(Query().equal("remoteCollectionId", ab.id).equal("etag", deletionChunk));
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
                bool isGroup = false;
                auto contact = ingestAddressDataNode(abDoc, node, isGroup);
                if (!contact) return;
                contact->setRemoteCollectionId(ab.id);
                if (isGroup) {
                    updatedGroups.push_back(contact);
                } else {
                    store->save(contact.get());
                }
            }));
            
            transaction.commit();
        }
    }
    
    // We need to write updated groups last so that they can update the contacts that they reference
    // Note: We save them down here so that if the sync proceess crashes or exits early we either
    // have full groups or we have nothing.
    for (auto contact : updatedGroups) {
        rebuildContactGroup(contact);
        store->save(contact.get());
    }
}

shared_ptr<Contact> DAVWorker::ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup) {
    auto etag = doc->nodeContentAtXPath(".//D:getetag/text()", node);
    auto href = doc->nodeContentAtXPath(".//D:href/text()", node);
    auto vcard = doc->nodeContentAtXPath(".//carddav:address-data/text()", node);
    if (vcard == "") {
        logger->info("Received addressbook entry {} with an empty body", etag);
        return nullptr;
    }
    
    auto belCard = BelCardParser::getInstance()->parseOne(vcard);
    if (belCard == NULL) {
        logger->info("Unable to decode vcard: {}", vcard);
        return nullptr;
    }
    string id = belCard->getUniqueId()->getValue();
    if (id == "") id = MailUtils::idForCalendar(account->id(), href);
    auto emailEntry = belCard->getEmails().front();
    string email = emailEntry == NULL ? "" : emailEntry->getValue();
    string name = belCard->getFullName()->getValue();
    if (name == "") name = belCard->getName()->getValue();
    
    auto contact = store->find<Contact>(Query().equal("id", id));
    if (!contact) {
        contact = make_shared<Contact>(id, account->id(), email, CONTACT_MAX_REFS, CARDDAV_SYNC_SOURCE);
    }
    contact->setInfo(json::object({{"vcf", vcard}, {"href", href}}));
    contact->setName(name);
    contact->setEmail(email);
    contact->setEtag(etag);
    
    isGroup = DAVUtils::isGroupCard(belCard);
    if (isGroup) {
        contact->setHidden(true);
    }
    return contact;
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
    
    string vcard = contact->info()["vcf"].get<string>();
    shared_ptr<BelCard> belCard = BelCardParser::getInstance()->parseOne(vcard);
    if (belCard == NULL) {
        return;
    }
    
    vector<string> members;
    
    // vcard4 members
    for (const auto & prop : belCard->getMembers()) {
        string mid = prop->getValue();
        if (mid.find("urn:uuid:") == 0) {
            mid = mid.substr(9);
        }
        members.push_back(mid);
    }
    // vcard3 members / icloud
    for (const auto & prop : belCard->getExtendedProperties()) {
        if (prop->getName()!= X_VCARD3_MEMBER) continue;
        string mid = prop->getValue();
        if (mid.find("urn:uuid:") == 0) {
            mid = mid.substr(9);
        }
        members.push_back(mid);
    }
    
    group->syncMembers(store, members);
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


struct curl_slist * DAVWorker::baseHeaders() {
    struct curl_slist *headers = NULL;

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
    return headers;
}

shared_ptr<DavXML> DAVWorker::performXMLRequest(string _url, string method, string payload) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;
    
    struct curl_slist *headers = baseHeaders();
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
    headers = curl_slist_append(headers, "Depth: 1");
    
    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 40);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    logger->info("{}: {} {}", _url, method, payload);
    string result = PerformRequest(curl_handle);
    logger->info("{}: {} {}\n{}", _url, method, payload, result);
    return make_shared<DavXML>(result, url);
}

string DAVWorker::performVCardRequest(string _url, string method, string vcard, string existingEtag) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;
    
    struct curl_slist *headers = baseHeaders();
    headers = curl_slist_append(headers, "Content-Type: text/vcard; charset=utf-8");
    if (existingEtag != "") {
        string etagHeader = "If-Match: " + existingEtag;
        headers = curl_slist_append(headers, etagHeader.c_str());
    }

    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = vcard.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 40);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    logger->info("{}: {} {}", _url, vcard, existingEtag);
    string result = PerformRequest(curl_handle);
    logger->info("{}: {}", _url, result);
    return result;
}

