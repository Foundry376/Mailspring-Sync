//
//  MetadataWorker.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef DAVWorker_hpp
#define DAVWorker_hpp

#include "Account.hpp"
#include "Identity.hpp"
#include "ContactBook.hpp"
#include "MailStore.hpp"
#include "DavXML.hpp"
#include "Event.hpp"
#include "Calendar.hpp"

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

typedef string ETAG;

class DAVWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

    string calHost;
    string calPrincipal;

    // In-memory discovery cache for CardDAV
    // Persists for lifetime of worker (days), reset on process restart
    bool contactsDiscoveryComplete = false;
    shared_ptr<ContactBook> cachedAddressBook = nullptr;
    int contactsValidationFailures = 0;

    bool validateCachedAddressBook();

    // Rate limiting state (shared for CalDAV and CardDAV)
    // Implements RFC 6585 (429) and RFC 7231 (Retry-After) compliance
    int backoffMs = 0;                      // Current backoff delay in milliseconds
    int consecutiveSuccesses = 0;           // Used to gradually reduce backoff
    time_t rateLimitedUntil = 0;            // Blocked until this time (from Retry-After)

    void applyRateLimitDelay();
    void recordRequestSuccess();
    void recordRateLimitResponse(int httpCode, const string& retryAfter);
    int parseRetryAfter(const string& retryAfter);

public:
    shared_ptr<Account> account;

    DAVWorker(shared_ptr<Account> account);
    
    void run();
    
    shared_ptr<ContactBook> resolveAddressBook();
    
    void writeAndResyncContact(shared_ptr<Contact> contact);
    void deleteContact(shared_ptr<Contact> contact);
    
    void runContacts();
    void runForAddressBook(shared_ptr<ContactBook> ab);
    bool runForAddressBookWithSyncToken(shared_ptr<ContactBook> ab, int retryCount = 0);

    void ingestContactDeletions(shared_ptr<ContactBook> ab, vector<ETAG> deleted);
    shared_ptr<Contact> ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(shared_ptr<Contact> contact);

    void runCalendars();
    void runForCalendar(string id, string name, string path);
    bool runForCalendarWithSyncToken(string calendarId, string url, shared_ptr<Calendar> calendar, int retryCount = 0);

    void writeAndResyncEvent(shared_ptr<Event> event);
    void deleteEvent(shared_ptr<Event> event);

    const string getAuthorizationHeader();

    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "", string depth = "1");
    string performVCardRequest(string _url, string method, string vcard = "", ETAG existingEtag = "");
    string performICSRequest(string url, string method, string icsData, ETAG existingEtag = "");
    
};

#endif /* DAVWorker_hpp */
