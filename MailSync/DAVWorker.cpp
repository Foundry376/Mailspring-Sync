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

struct EventResult {
    std::string icsHref;
    ETAG etag;
};



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
    calHost = "";
    calPrincipal = "discover";

    // Shortcuts for a few providers that implement CardDav / CalDav but do NOT
    // expose SRV records telling us where they are.
    if (account->provider() == "gmail") {
        calHost = "apidata.googleusercontent.com";
        calPrincipal = "/caldav/v2/" + account->emailAddress();
    }
    
    if (account->IMAPHost().find("imap.mail.ru") != string::npos) {
        calHost = "calendar.mail.ru";
        calPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.yandex.com") != string::npos) {
        calHost = "yandex.ru";
        calPrincipal = "discover";
    }
    if (account->IMAPHost().find("securemail.a1.net") != string::npos) {
        calHost = "caldav.a1.net";
        calPrincipal = "discover";
    }
    if (account->IMAPHost().find("imap.zoho.com") != string::npos) {
        calHost = "calendar.zoho.com";
        calPrincipal = "discover";
    }

    // Initialize libxml
    xmlInitParser();
}

void DAVWorker::run() {
    runContacts();
    runCalendars();
}

void DAVWorker::runContacts() {
    if (account->provider() == "gmail") {
        // Gmail sync uses a separate GoogleContactsWorker
        return;
    }
    auto ab = resolveAddressBook();
    if (ab != nullptr) {
        runForAddressBook(ab);
    }
}

/*
 Currently we load whatever contact book we have saved locally, but still re-run the DNS + well-known
 search to see if we need to update it's URL since users have no way to do this manually.
*/
shared_ptr<ContactBook> DAVWorker::resolveAddressBook() {
    shared_ptr<ContactBook> existing = store->find<ContactBook>(Query().equal("accountId", account->id()));
    if (existing && existing->source() != CARDDAV_SYNC_SOURCE) {
        return nullptr;
    }
    
    string cardHost = "";
    
    // Try to use DNS SRV records to find the principal URL. We do this through a server API so that
    // we don't have to compile C++ that does DNS lookups. On Win it's a pain and on Linux it generates
    // a binary that is bound to a specific version of glibc which generates relocation errors when
    // run on Ubuntu 18.
    string domain = account->emailAddress().substr(account->emailAddress().find("@") + 1);
    string imapHost = account->IMAPHost();
    json payload = {{"domain", domain}, {"imapHost", imapHost}};
    json result = PerformJSONRequest(CreateIdentityRequest("/api/resolve-dav-hosts", "POST", payload.dump().c_str()));
    
    if (result.count("carddavHost")) {
        cardHost = result["carddavHost"].get<string>();
    }
    
    if (cardHost == "") {
        // No luck.
        return existing;
    }

    // Use the .well-known convention to try to look up the root path of the carddav service at the host. If this doesn't
    // work, that's fine with us, we just try the root.
    string cardRoot = PerformExpectedRedirect("https://" + cardHost + "/.well-known/carddav");
    if (cardRoot == "") {
        cardRoot = PerformExpectedRedirect("http://" + cardHost + "/.well-known/carddav");
        if (cardRoot == "" || cardRoot.find("/.well-known") != string::npos) {
            // if we couldn't find the root or the redirect looks like it was sending us in a circle,
            // (or redirecting us to https://) fall back to the root.
            cardRoot = cardHost + "/";
        }
    }
    
    if (cardRoot == "https://mail.yahoo.com/") {
        // workaound yahoo second recirect above sending us to mail.yahoo.com...
        return existing;
    }
    
    // Fetch the current user principal URL from the CardDav root
    auto principalDoc = performXMLRequest(cardRoot, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><A:propfind xmlns:A=\"DAV:\"><A:prop><A:current-user-principal/><A:principal-URL/><A:resourcetype/></A:prop></A:propfind>");
    string cardPrincipal = principalDoc->nodeContentAtXPath("//D:current-user-principal/D:href/text()");
    if (cardPrincipal.find("://") == string::npos) {
        cardPrincipal = replacePath(cardRoot, cardPrincipal);
    }
    
    // Fetch the address book home set URL from the user principal URL
    auto abSetDoc = performXMLRequest(cardPrincipal, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><A:propfind xmlns:A=\"DAV:\"><A:prop><A:displayname/><A:resourcetype/><B:addressbook-home-set xmlns:B=\"urn:ietf:params:xml:ns:carddav\"/></A:prop></A:propfind>");
    auto abSetURL = abSetDoc->nodeContentAtXPath("//carddav:addressbook-home-set/D:href/text()");
    if (abSetURL.find("://") == string::npos) {
        abSetURL = replacePath(cardRoot, abSetURL);
    }
    
    // Hit the home address book set to retrieve the individual address books
    auto abSetContentsDoc = performXMLRequest(abSetURL, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\"><d:prop><d:resourcetype /></d:prop></d:propfind>");
                                              
    // Iterate over the address books and run a sync on each one
    // TODO: Pick the primary one somehow!
    
    abSetContentsDoc->evaluateXPath("//D:response[.//carddav:addressbook]", ([&](xmlNodePtr node) {
        string abHREF = abSetContentsDoc->nodeContentAtXPath(".//D:href/text()", node);
        string abURL = (abHREF.find("://") == string::npos) ? replacePath(abSetURL, abHREF) : abHREF;
        if (!existing) {
            existing = make_shared<ContactBook>(account->id() + "-default", account->id());
        }
        existing->setSource("carddav");
        existing->setURL(abURL);
        store->save(existing.get());
    }));
    
    return existing;
}

void DAVWorker::writeAndResyncContact(shared_ptr<Contact> contact) {
    shared_ptr<ContactBook> ab = store->find<ContactBook>(Query().equal("accountId", account->id()));

    if (ab == nullptr) {
        logger->warn("No address book found.");
        return;
    }

    string vcf = contact->info()["vcf"].get<string>();
    string href = contact->info().count("href") ? contact->info()["href"].get<string>() : "";
    
    // write the card
    if (href == "") {
        string result = performVCardRequest(ab->url(), "POST", vcf);
        auto postDoc = make_shared<DavXML>(result, ab->url());
        href = postDoc->nodeContentAtXPath("//D:href/text()");

    } else {
        try {
        performVCardRequest(replacePath(ab->url(), href), "PUT", vcf, contact->etag());
        } catch (SyncException & e) {
            if (e.key.find("403") != string::npos) {
                // silently continue so our "Bad" vcard is immediately reverted below.
            } else {
                throw;
            }
        }
    }
    
    if (href != "") {
        // read the card back to ingest server-side changes and the new etag
        // IMPORTANT: We receive a new copy of this contact with possibly new data.
        // DO NOT SAVE the one in the outer scope!
        auto abDoc = performXMLRequest(ab->url(), "REPORT", "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /><c:address-data /></d:prop><d:href>"+href+"</d:href></c:addressbook-multiget>");
        abDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            bool isGroup = false;
            auto serverside = ingestAddressDataNode(abDoc, node, isGroup);
            if (!serverside) {
                logger->warn("Not able to inflate contact from REPORT response after sending.");
            }
            serverside->setBookId(ab->id());
            if (isGroup) {
                rebuildContactGroup(serverside);
            }
            logger->info("Contact ETAG is now {}", serverside->etag());
            store->save(serverside.get());
            
            // If the server re-assigned the contact a new ID / UID (FastMail),
            // blow away our old contact model because the save above didn't update it.
            if (contact->id() != serverside->id()) {
                store->remove(contact.get());
            }
        }));
    } else {
        // sync probably failed
        logger->warn("Not able to retrieve contact after sending, no href");

    }
}

void DAVWorker::deleteContact(shared_ptr<Contact> contact) {
    if (contact->source() != CARDDAV_SYNC_SOURCE) {
        logger->info("Deleted contact not synced via CardDAV");
        return;
    }

    shared_ptr<ContactBook> ab = store->find<ContactBook>(Query().equal("accountId", account->id()));
    string vcf = contact->info()["vcf"].get<string>();
    
    if (ab == nullptr) {
        logger->warn("No address book found.");
        return;
    }

    if (contact->info().count("href") == 0) {
        logger->warn("No href in info.");
        return;
    }

    string href = contact->info()["href"].get<string>();
    if (href == "") {
       logger->warn("Blank href in info.");
       return;
    }
    
    performVCardRequest(replacePath(ab->url(), href), "DELETE", "", contact->etag());
    store->remove(contact.get());
}

void DAVWorker::runForAddressBook(shared_ptr<ContactBook> ab) {
    map<ETAG, string> remote {};

    {
        auto etagsDoc = performXMLRequest(ab->url(), "REPORT", "<c:addressbook-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /></d:prop></c:addressbook-query>");

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
        SQLite::Statement findEtags(store->db(), "SELECT etag FROM Contact WHERE bookId = ?");
        findEtags.bind(1, ab->id());
        while (findEtags.executeStep()) {
            local[findEtags.getColumn("etag")] = true;
        }
    }

    // identify new and deleted events
    vector<ETAG> deleted {};
    vector<ETAG> needed {};
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
        logger->info("{}", ab->url());
        logger->info("  remote: {} etags", remote.size());
        logger->info("   local: {} etags", local.size());
        logger->info(" deleted: {}", deleted.size());
        logger->info("  needed: {}", needed.size());
    }
    
    vector<shared_ptr<Contact>> updatedGroups;
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 90)) {
        string payload = "";
        for (auto & href : chunk) {
            payload += "<d:href>" + href + "</d:href>";
        }
        
        // Fetch the data
        auto abDoc = performXMLRequest(ab->url(), "REPORT", "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag /><c:address-data /></d:prop>" + payload + "</c:addressbook-multiget>");
        
        // Insert the event objects and remove deleted events within the same transaction.
        // Most of the time, this results in an event being replaced within a single transaction.
        {
            MailStoreTransaction transaction {store, "runForAddressBook"};
            
            if (deleted.size()) {
                ingestContactDeletions(ab, deleted);
                deleted.clear();
            }
            
            abDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                bool isGroup = false;
                auto contact = ingestAddressDataNode(abDoc, node, isGroup);
                if (!contact) return;
                contact->setBookId(ab->id());
                if (isGroup) {
                    updatedGroups.push_back(contact);
                } else {
                    store->save(contact.get());
                }
            }));
            
            transaction.commit();
        }
    }

    // The code above only procsses deletions if it's also processing upserts, so we need to do
    // a final check for deletions here.
    if (deleted.size()) {
        ingestContactDeletions(ab, deleted);
        deleted.clear();
    }
    
    // We need to write updated groups last so that they can update the contacts that they reference
    // Note: We save them down here so that if the sync proceess crashes or exits early we either
    // have full groups or we have nothing.
    for (auto contact : updatedGroups) {
        rebuildContactGroup(contact);
        store->save(contact.get());
    }
}

void DAVWorker::ingestContactDeletions(shared_ptr<ContactBook> ab, vector<string> deleted) {
    if (deleted.size() == 0) {
        return;
    }
    auto deletionChunks = MailUtils::chunksOfVector(deleted, 100);

    for (auto & deletionChunk : deletionChunks) {
        // Delete the contacts
        vector<string> possibleGroupIds {};
        auto deletedItem = store->findAll<Contact>(Query().equal("bookId", ab->id()).equal("etag", deletionChunk));
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

shared_ptr<Contact> DAVWorker::ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup) {
    auto etag = doc->nodeContentAtXPath(".//D:getetag/text()", node);
    auto href = doc->nodeContentAtXPath(".//D:href/text()", node);
    auto vcardString = doc->nodeContentAtXPath(".//carddav:address-data/text()", node);
    if (vcardString == "") {
        logger->info("Received addressbook entry {} with an empty body", etag);
        return nullptr;
    }
    
    auto vcard = make_shared<VCard>(vcardString);
    if (vcard->incomplete()) {
        logger->info("Unable to decode vcard: {}", vcardString);
        return nullptr;
    }
    string id = vcard->getUniqueId()->getValue();
    if (id == "") id = MailUtils::idForCalendar(account->id(), href);
    string email = vcard->getEmails().front()->getValue();
    string name = vcard->getFormattedName()->getValue();
    if (name == "") name = vcard->getName()->getValue();
    
    auto contact = store->find<Contact>(Query().equal("id", id));
    if (!contact) {
        contact = make_shared<Contact>(id, account->id(), email, CONTACT_MAX_REFS, CARDDAV_SYNC_SOURCE);
    }
    contact->setInfo(json::object({{"vcf", vcardString}, {"href", href}}));
    contact->setName(name);
    contact->setEmail(email);
    contact->setEtag(etag);
    
    isGroup = DAVUtils::isGroupCard(vcard);
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
    group->setBookId(contact->bookId());
    store->save(group.get());
    
    shared_ptr<VCard> vcard = make_shared<VCard>(contact->info()["vcf"].get<string>());
    if (vcard->incomplete()) {
        return;
    }
    
    vector<string> members;
    
    // vcard4 members
    for (const auto & prop : vcard->getMembers()) {
        string mid = prop->getValue();
        if (mid.find("urn:uuid:") == 0) {
            mid = mid.substr(9);
        }
        members.push_back(mid);
    }
    // vcard3 members / icloud
    for (const auto & prop : vcard->getExtendedProperties()) {
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
    
    for (auto chunk : MailUtils::chunksOfVector(needed, 90)) {
        string payload = "";
        for (auto & icsHref : chunk) {
            payload += "<D:href>" + icsHref + "</D:href>";
        }

        // Debounce 1sec on each request because Google has a cap on total queries
        // per day and we want to avoid somehow allowing one client to make low-latency
        // requests so fast that it blows through the limit in a short time.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Fetch the data
        auto icsDoc = performXMLRequest(url, "REPORT", "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /><c:calendar-data /></d:prop>" + payload + "</c:calendar-multiget>");
        
        // Insert the event objects and remove deleted events within the same transaction.
        // Most of the time, this results in an event being replaced within a single transaction.
        {
            MailStoreTransaction transaction {store, "runForCalendar:insertions"};
            
            icsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                auto etag = icsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                if (etag != "" && local.count(etag) == 0) {
                    auto icsData = icsDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);
                    if (icsData != "") {
                        local[etag] = true;
                        ICalendar cal(icsData);
                        
                        auto icsEvent = cal.Events.front();
                        if (!icsEvent->DtStart.IsEmpty()) {
                            auto event = Event(etag, account->id(), calendarId, icsData, icsEvent);
                            store->save(&event);
                        } else {
                            logger->info("Received calendar event but it has no start time?\n\n{}\n\n", icsData);
                        }
                    } else {
                        logger->info("Received calendar event {} with an empty body", etag);
                    }
                } else {
                    // we didn't ask for this, or we already received it in this session.
                    
                    // Note: occasionally (with Google Cal at least), the initial "index" query returns etag+href pairs,
                    // but then requesting them individually returns etag="", icsdata="". It seems like some data consistency
                    // issue on their side and for now we just ignore them. There are two in bengotow@gmail.com.
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
        MailStoreTransaction transaction {store, "runForCalendar:deletions"};
        for (auto & deletionChunk : deletionChunks) {
            auto deletionEvents = store->findAll<Event>(Query().equal("calendarId", calendarId).equal("etag", deletionChunk));
            for (auto & e : deletionEvents) {
                store->remove(e.get());
            }
        }
        transaction.commit();
    }
}


const string DAVWorker::getAuthorizationHeader() {
    if (account->refreshToken() != "") {
        auto parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        return "Authorization: Bearer " + parts.accessToken;
    }
    
    string plain = account->IMAPUsername() + ":" + account->IMAPPassword();
    string encoded = MailUtils::toBase64(plain.c_str(), strlen(plain.c_str()));
    return "Authorization: Basic " + encoded;
}

shared_ptr<DavXML> DAVWorker::performXMLRequest(string _url, string method, string payload) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, getAuthorizationHeader().c_str());
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
    
    if (payload.find("xml:ns:carddav") != string::npos) {
        headers = curl_slist_append(headers, "Accept: text/vcard; version=4.0");
    }
    headers = curl_slist_append(headers, "Depth: 1");
    
    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 40);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);
    
    string result = PerformRequest(curl_handle);
    return make_shared<DavXML>(result, url);
}

string DAVWorker::performVCardRequest(string _url, string method, string vcard, ETAG existingEtag) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, getAuthorizationHeader().c_str());
    headers = curl_slist_append(headers, "Content-Type: text/vcard; charset=utf-8");
    headers = curl_slist_append(headers, "Accept: text/vcard; version=4.0");
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
    return result;
}

