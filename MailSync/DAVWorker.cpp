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
#include "icalendar.h"

#include <string>
#include <set>
#include <curl/curl.h>

struct EventResult {
    std::string icsHref;
    ETAG etag;
};

// Calendar sync time range configuration
// Events outside this range are not synced to reduce storage and improve performance
static const int CALENDAR_SYNC_PAST_MONTHS = 12;
static const int CALENDAR_SYNC_FUTURE_MONTHS = 18;

struct CalendarSyncRange {
    time_t start;
    time_t end;
    string startStr;  // Formatted for CalDAV time-range filter
    string endStr;
};

CalendarSyncRange getCalendarSyncRange() {
    time_t now = time(nullptr);
    time_t start = now - (CALENDAR_SYNC_PAST_MONTHS * 30 * 24 * 60 * 60);
    time_t end = now + (CALENDAR_SYNC_FUTURE_MONTHS * 30 * 24 * 60 * 60);
    return {
        start,
        end,
        MailUtils::formatDateTimeUTC(start),
        MailUtils::formatDateTimeUTC(end)
    };
}

bool eventOverlapsRange(time_t eventStart, time_t eventEnd, const CalendarSyncRange& range) {
    // Event overlaps range if it ends after range start AND starts before range end
    return eventEnd >= range.start && eventStart <= range.end;
}

static string firstPropVal(json attrs, string key, string fallback = "") {
    if (!attrs.count(key)) return fallback;
    if (!attrs[key].size()) return fallback;
    return attrs[key][0].get<string>();
}

static string replacePath(string url, string path) {
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

// URL decode a percent-encoded string (e.g., "%20" -> " ", "%2F" -> "/")
static string urlDecode(const string & encoded) {
    string result;
    result.reserve(encoded.length());

    for (size_t i = 0; i < encoded.length(); i++) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            // Check if next two chars are valid hex digits
            char h1 = encoded[i + 1];
            char h2 = encoded[i + 2];
            if (isxdigit(h1) && isxdigit(h2)) {
                // Convert hex to char
                int val = 0;
                if (h1 >= '0' && h1 <= '9') val = (h1 - '0') << 4;
                else if (h1 >= 'a' && h1 <= 'f') val = (h1 - 'a' + 10) << 4;
                else if (h1 >= 'A' && h1 <= 'F') val = (h1 - 'A' + 10) << 4;

                if (h2 >= '0' && h2 <= '9') val |= (h2 - '0');
                else if (h2 >= 'a' && h2 <= 'f') val |= (h2 - 'a' + 10);
                else if (h2 >= 'A' && h2 <= 'F') val |= (h2 - 'A' + 10);

                result += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        // Handle '+' as space (common in query strings, though rare in DAV)
        if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}

// Normalize href for comparison - extracts path portion and handles encoding differences
static string normalizeHref(const string & href) {
    string result = href;

    // Strip scheme and host if present (e.g., "https://server.com/path" -> "/path")
    size_t schemeEnd = result.find("://");
    if (schemeEnd != string::npos) {
        size_t pathStart = result.find("/", schemeEnd + 3);
        if (pathStart != string::npos) {
            result = result.substr(pathStart);
        }
    }

    // URL decode the path to handle %XX encoding differences
    result = urlDecode(result);

    // Remove trailing slashes for consistency
    while (result.length() > 1 && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

// Rate limiting constants (RFC 6585/7231 compliance)
static const int MAX_BACKOFF_MS = 60000;  // 1 minute max backoff
static const int MIN_BACKOFF_MS = 100;    // 100ms minimum when backing off

// Thread-local storage for capturing response headers during curl requests
// Used to extract Retry-After header for rate limiting
thread_local string capturedHeaders;

size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalSize = size * nitems;
    capturedHeaders.append(buffer, totalSize);
    return totalSize;
}

// Extract a header value from captured headers (case-insensitive)
static string extractHeader(const string& headers, const string& headerName) {
    string searchFor = headerName + ":";
    size_t pos = 0;

    // Case-insensitive search
    string headersLower = headers;
    string searchLower = searchFor;
    transform(headersLower.begin(), headersLower.end(), headersLower.begin(), ::tolower);
    transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    pos = headersLower.find(searchLower);
    if (pos == string::npos) {
        return "";
    }

    // Find the value after the colon
    size_t valueStart = pos + searchFor.length();
    size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == string::npos) {
        valueEnd = headers.find("\n", valueStart);
    }
    if (valueEnd == string::npos) {
        valueEnd = headers.length();
    }

    string value = headers.substr(valueStart, valueEnd - valueStart);

    // Trim whitespace
    size_t start = value.find_first_not_of(" \t");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == string::npos) return "";
    return value.substr(start, end - start + 1);
}

// Parse Retry-After header value (RFC 7231)
// Supports both delay-seconds (integer) and HTTP-date formats
int DAVWorker::parseRetryAfter(const string& retryAfter) {
    if (retryAfter.empty()) {
        return 0;
    }

    // Try parsing as integer (delay-seconds format)
    try {
        int seconds = stoi(retryAfter);
        if (seconds > 0) {
            return seconds;
        }
    } catch (...) {
        // Not an integer, try parsing as HTTP-date
    }

    // Try parsing as HTTP-date (e.g., "Fri, 31 Dec 2024 23:59:59 GMT")
    struct tm tm = {};
    if (strptime(retryAfter.c_str(), "%a, %d %b %Y %H:%M:%S", &tm) != nullptr) {
        time_t targetTime = timegm(&tm);
        time_t now = time(nullptr);
        if (targetTime > now) {
            return static_cast<int>(targetTime - now);
        }
    }

    return 0;
}

void DAVWorker::applyRateLimitDelay() {
    time_t now = time(nullptr);

    // If we're blocked by Retry-After, wait until that time
    if (rateLimitedUntil > now) {
        int waitSeconds = static_cast<int>(rateLimitedUntil - now);
        logger->info("Rate limited: waiting {} seconds (Retry-After)", waitSeconds);
        this_thread::sleep_for(chrono::seconds(waitSeconds));
    }

    // Apply exponential backoff delay if any
    if (backoffMs > 0) {
        logger->info("Rate limit backoff: waiting {}ms", backoffMs);
        this_thread::sleep_for(chrono::milliseconds(backoffMs));
    }
}

void DAVWorker::recordRequestSuccess() {
    consecutiveSuccesses++;

    // After 3 consecutive successes, reduce backoff by half
    if (consecutiveSuccesses >= 3 && backoffMs > 0) {
        backoffMs = backoffMs / 2;
        consecutiveSuccesses = 0;

        if (backoffMs < MIN_BACKOFF_MS) {
            backoffMs = 0;
            logger->info("Rate limit backoff cleared");
        }
    }
}

void DAVWorker::recordRateLimitResponse(int httpCode, const string& retryAfter) {
    consecutiveSuccesses = 0;

    // Parse Retry-After header if present
    int retrySeconds = parseRetryAfter(retryAfter);

    if (retrySeconds > 0) {
        // Server specified a wait time
        rateLimitedUntil = time(nullptr) + retrySeconds;
        logger->warn("Rate limited ({}): server requested {} second wait", httpCode, retrySeconds);
    } else {
        // No Retry-After header, use exponential backoff
        if (backoffMs == 0) {
            backoffMs = MIN_BACKOFF_MS;
        } else {
            backoffMs = min(backoffMs * 2, MAX_BACKOFF_MS);
        }
        logger->warn("Rate limited ({}): using exponential backoff ({}ms)", httpCode, backoffMs);
    }
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

    // Discovery already completed but no address book found - nothing to sync
    if (contactsDiscoveryComplete && cachedAddressBook == nullptr) {
        return;
    }

    // First sync or after cache invalidation: perform full discovery
    if (!contactsDiscoveryComplete) {
        logger->info("Performing full CardDAV address book discovery");
        cachedAddressBook = resolveAddressBook();
        contactsDiscoveryComplete = true;
        contactsValidationFailures = 0;

        if (cachedAddressBook == nullptr) {
            return;
        }
    }

    // Subsequent syncs: validate cached URL with lightweight request
    if (!validateCachedAddressBook()) {
        // Cache invalid, clear and re-discover next cycle
        logger->info("Cached address book URL invalid, will rediscover on next sync");
        cachedAddressBook = nullptr;
        contactsDiscoveryComplete = false;
        return;
    }

    // Get the old ctag from the database for comparison
    shared_ptr<ContactBook> existingAb = store->find<ContactBook>(Query().equal("accountId", account->id()));
    string oldCtag = existingAb ? existingAb->ctag() : "";
    string newCtag = cachedAddressBook->ctag();

    // Compare old ctag with new ctag from server
    if (oldCtag == newCtag && newCtag != "") {
        logger->info("Address book unchanged (ctag: {}), skipping sync", newCtag);
        return;
    }

    if (newCtag != "") {
        logger->info("Syncing address book (ctag: {} -> {})", oldCtag, newCtag);
    } else {
        logger->info("Syncing address book (server doesn't provide ctag)");
    }

    // Try sync-token first (RFC 6578), fall back to legacy etag-based sync
    bool usedSyncToken = runForAddressBookWithSyncToken(cachedAddressBook);
    if (!usedSyncToken) {
        runForAddressBook(cachedAddressBook);
    }
}

/*
 Validates that the cached address book URL is still accessible.
 Uses a lightweight PROPFIND with Depth: 0 to check URL validity and fetch current ctag.
 Returns true if URL is valid, false if cache should be invalidated.
 Throws on transient errors (network issues) to retry next cycle.
*/
bool DAVWorker::validateCachedAddressBook() {
    if (cachedAddressBook == nullptr) {
        return false;
    }

    try {
        // Lightweight PROPFIND for ctag only (depth 0 = single resource)
        // This validates the URL is still accessible without fetching all contacts
        auto doc = performXMLRequest(
            cachedAddressBook->url(),
            "PROPFIND",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\">"
            "<d:prop><cs:getctag/></d:prop>"
            "</d:propfind>",
            "0"
        );

        // Extract and update ctag from validation response
        string ctag = doc->nodeContentAtXPath("//cs:getctag/text()");
        if (ctag != "") {
            cachedAddressBook->setCtag(ctag);
        }

        // Success - reset failure counter
        contactsValidationFailures = 0;
        return true;

    } catch (const SyncException& e) {
        contactsValidationFailures++;

        // URL definitively invalid (deleted, moved, etc.)
        if (e.key.find("404") != string::npos ||
            e.key.find("410") != string::npos) {
            logger->info("Address book URL returned {}, invalidating cache", e.key);
            return false;
        }

        // Auth failures - retry a few times before giving up
        if (e.key.find("401") != string::npos ||
            e.key.find("403") != string::npos) {
            if (contactsValidationFailures >= 3) {
                logger->warn("Multiple auth failures ({}), invalidating discovery cache",
                    contactsValidationFailures);
                return false;
            }
            // Propagate error to retry next cycle, keep cache for now
            throw;
        }

        // Network errors - propagate, don't invalidate cache
        throw;
    }
}

/*
 Performs full address book discovery via DNS SRV + .well-known + PROPFIND chain.
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
    
    // Hit the home address book set to retrieve the individual address books (including ctag)
    auto abSetContentsDoc = performXMLRequest(abSetURL, "PROPFIND", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\"><d:prop><d:resourcetype /><d:displayname /><cs:getctag /></d:prop></d:propfind>");

    // Iterate over the address books and run a sync on each one
    // TODO: Pick the primary one somehow!

    abSetContentsDoc->evaluateXPath("//D:response[.//carddav:addressbook]", ([&](xmlNodePtr node) {
        string abHREF = abSetContentsDoc->nodeContentAtXPath(".//D:href/text()", node);
        string abURL = (abHREF.find("://") == string::npos) ? replacePath(abSetURL, abHREF) : abHREF;
        string ctag = abSetContentsDoc->nodeContentAtXPath(".//cs:getctag/text()", node);
        if (!existing) {
            existing = make_shared<ContactBook>(account->id() + "-default", account->id());
        }
        existing->setSource("carddav");
        existing->setURL(abURL);
        existing->setCtag(ctag);
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
        // Read the card back to ingest server-side changes and the new etag.
        // IMPORTANT: We receive a new copy of this contact with possibly new data.
        // DO NOT SAVE the one in the outer scope!
        //
        // We use multiget REPORT instead of a direct GET request for Zimbra compatibility.
        // See comments in runForAddressBook for details on the "event_by_url_is_broken" provider quirk.
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

        // Fetch contact data using REPORT addressbook-multiget (RFC 6352 section 8.7).
        //
        // Important: We always use multiget instead of direct GET requests. This is a deliberate
        // design decision that ensures compatibility with servers like Zimbra that have broken
        // GET implementations. Zimbra returns 404 errors for direct GET requests to valid vCard
        // URLs, but the same URLs work correctly when requested via multiget REPORT.
        //
        // The Python caldav library implements a fallback strategy (try GET, fallback to multiget),
        // but we skip straight to multiget for reliability. The multiget approach also has
        // performance benefits by fetching multiple items in a single HTTP request.
        //
        // See: python-caldav compatibility hints for "event_by_url_is_broken" flag.
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

bool DAVWorker::runForAddressBookWithSyncToken(shared_ptr<ContactBook> ab, int retryCount) {
    const int maxRetries = 1; // Allow one retry for token expiration

    string syncToken = ab->syncToken();
    bool isInitialSync = syncToken.empty();

    // Accumulated data across pagination requests
    vector<string> neededHrefs;
    vector<string> deletedHrefs;
    vector<shared_ptr<Contact>> updatedGroups;

    // RFC 6578 pagination: loop until we get all results
    // Server returns 507 status when results are truncated
    bool hasMorePages = true;
    int pageCount = 0;
    const int maxPages = 100; // Safety limit to prevent infinite loops

    while (hasMorePages && pageCount < maxPages) {
        pageCount++;

        // Build sync-collection request per RFC 6578
        string tokenElement = syncToken.empty() ? "<D:sync-token/>" : "<D:sync-token>" + syncToken + "</D:sync-token>";

        // For incremental sync (with token), request full data since changeset is expected to be small
        // For initial sync (empty token), request only etags, then use multiget for data in chunks
        string props = isInitialSync
            ? "<D:getetag/>"
            : "<D:getetag/><C:address-data/>";

        string query = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<D:sync-collection xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:carddav\">"
            + tokenElement +
            "<D:sync-level>1</D:sync-level>"
            "<D:prop>" + props + "</D:prop>"
            "</D:sync-collection>";

        shared_ptr<DavXML> syncDoc;
        bool http507Received = false;
        try {
            // RFC 6578 requires Depth: 0 for sync-collection
            syncDoc = performXMLRequest(ab->url(), "REPORT", query, "0");
        } catch (SyncException & e) {
            // Check for HTTP 507 (Insufficient Storage) - some servers return this as HTTP status
            // instead of embedding it in the 207 Multi-Status response per RFC 6578
            if (e.key.find("507") != string::npos) {
                size_t returnedPos = e.debuginfo.find(" RETURNED ");
                if (returnedPos != string::npos) {
                    string responseBody = e.debuginfo.substr(returnedPos + 10);
                    try {
                        syncDoc = make_shared<DavXML>(responseBody, ab->url());
                        http507Received = true;
                        logger->info("Received HTTP 507, parsing truncated response");
                    } catch (...) {
                        logger->warn("Failed to parse HTTP 507 response body");
                    }
                }
            }

            // If we successfully parsed HTTP 507 response, continue processing
            if (syncDoc) {
                // Fall through to normal processing below
            } else if (e.key.find("403") != string::npos ||
                       e.key.find("409") != string::npos ||
                       e.key.find("410") != string::npos ||
                       e.debuginfo.find("valid-sync-token") != string::npos) {
                // Token expiration - retry with empty token if we haven't exceeded retries
                if (syncToken.empty() || retryCount >= maxRetries) {
                    if (retryCount >= maxRetries) {
                        logger->warn("Max retries ({}) exceeded for address book sync token, falling back to legacy sync", maxRetries);
                    }
                    logger->info("sync-collection not supported or failed for contacts, using legacy sync");
                    return false;
                }
                logger->info("Sync token expired for address book, retrying with full sync (attempt {})", retryCount + 1);
                ab->setSyncToken("");
                store->save(ab.get());
                return runForAddressBookWithSyncToken(ab, retryCount + 1);
            } else {
                // Server doesn't support sync-collection or other error
                logger->info("sync-collection not supported or failed for contacts, using legacy sync");
                return false;
            }
        }

        // Extract new sync-token from response
        string newSyncToken = syncDoc->nodeContentAtXPath("//D:sync-token/text()");

        // Check for 507 (Insufficient Storage) status indicating truncated results
        // RFC 6578 section 3.6: server returns 507 when it can't return all results
        // This can come either as HTTP 507 (handled above) or embedded in 207 response
        hasMorePages = http507Received;
        syncDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            auto href = syncDoc->nodeContentAtXPath(".//D:href/text()", node);
            auto status = syncDoc->nodeContentAtXPath(".//D:status/text()", node);

            if (status.find("507") != string::npos) {
                // Truncated response - more pages available (embedded in 207 Multi-Status per RFC 6578)
                hasMorePages = true;
                logger->info("sync-collection page {} truncated (507 in response), continuing...", pageCount);
            } else if (status.find("404") != string::npos) {
                // Item was deleted on server (RFC 6578 section 3.5)
                deletedHrefs.push_back(href);
            } else {
                auto etag = syncDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                auto vcardData = syncDoc->nodeContentAtXPath(".//carddav:address-data/text()", node);

                if (vcardData != "") {
                    // Have full data (incremental sync response) - process directly
                    bool isGroup = false;
                    auto contact = ingestAddressDataNode(syncDoc, node, isGroup);
                    if (contact) {
                        contact->setBookId(ab->id());
                        if (isGroup) {
                            updatedGroups.push_back(contact);
                        } else {
                            store->save(contact.get());
                        }
                    }
                } else if (etag != "") {
                    // Need to fetch data (initial sync response with etags only)
                    neededHrefs.push_back(href);
                }
            }
        }));

        // Update sync token for next iteration (or final storage)
        if (newSyncToken != "") {
            syncToken = newSyncToken;
        }
    }

    if (pageCount >= maxPages) {
        logger->warn("sync-collection hit max pages limit ({}), sync may be incomplete", maxPages);
    }

    logger->info("sync-collection complete ({} pages) for contacts: {} needed, {} deleted",
                 pageCount, neededHrefs.size(), deletedHrefs.size());

    // Fetch needed items (from initial sync) using multiget in chunks.
    // See comments in runForAddressBook for why we use multiget instead of direct GET requests.
    if (!neededHrefs.empty()) {
        std::reverse(neededHrefs.begin(), neededHrefs.end());

        for (auto chunk : MailUtils::chunksOfVector(neededHrefs, 90)) {
            string payload = "";
            for (auto & href : chunk) {
                payload += "<d:href>" + href + "</d:href>";
            }

            auto abDoc = performXMLRequest(ab->url(), "REPORT",
                "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\">"
                "<d:prop><d:getetag /><c:address-data /></d:prop>" + payload + "</c:addressbook-multiget>");

            MailStoreTransaction transaction{store, "syncToken:contacts:multiget"};
            abDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                bool isGroup = false;
                auto contact = ingestAddressDataNode(abDoc, node, isGroup);
                if (contact) {
                    contact->setBookId(ab->id());
                    if (isGroup) {
                        updatedGroups.push_back(contact);
                    } else {
                        store->save(contact.get());
                    }
                }
            }));
            transaction.commit();
        }
    }

    // Delete removed items by href - load all contacts once, then find matches
    if (!deletedHrefs.empty()) {
        MailStoreTransaction transaction{store, "syncToken:contacts:deletions"};

        // Load all contacts from this address book once (href is in JSON data column, can't query directly)
        auto allContacts = store->findAll<Contact>(Query().equal("bookId", ab->id()));

        // Build a set of normalized hrefs for O(1) lookup
        set<string> deletedHrefSet;
        for (auto & href : deletedHrefs) {
            deletedHrefSet.insert(normalizeHref(href));
        }

        // Find and delete matching contacts
        for (auto & c : allContacts) {
            if (c->info().count("href")) {
                string contactHref = normalizeHref(c->info()["href"].get<string>());
                if (deletedHrefSet.count(contactHref)) {
                    // Also delete associated group if exists
                    auto group = store->find<ContactGroup>(Query().equal("id", c->id()));
                    if (group) {
                        store->remove(group.get());
                    }
                    store->remove(c.get());
                }
            }
        }
        transaction.commit();
    }

    // Process groups after all contacts are saved
    for (auto contact : updatedGroups) {
        rebuildContactGroup(contact);
        store->save(contact.get());
    }

    // Store final sync-token for next sync
    if (syncToken != "" && syncToken != ab->syncToken()) {
        ab->setSyncToken(syncToken);
        store->save(ab.get());
        logger->info("Stored new sync-token for address book");
    }

    return true;
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
    // Request calendar metadata: color, description, read-only status, order
    string propfindQuery =
        "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\" "
        "xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:ical=\"http://apple.com/ns/ical/\">"
        "<d:prop>"
        "<d:resourcetype />"
        "<d:displayname />"
        "<cs:getctag />"
        "<c:supported-calendar-component-set />"
        "<ical:calendar-color />"
        "<c:calendar-description />"
        "<d:current-user-privilege-set />"
        "<ical:calendar-order />"
        "</d:prop>"
        "</d:propfind>";
    auto calendarSetDoc = performXMLRequest(calHost + calPrincipal, "PROPFIND", propfindQuery);

    auto local = store->findAllMap<Calendar>(Query().equal("accountId", account->id()), "id");
    
    // Iterate over the calendars that expose "VEVENT" components
    calendarSetDoc->evaluateXPath("//D:response[./D:propstat/D:prop/caldav:supported-calendar-component-set/caldav:comp[@name='VEVENT']]", ([&](xmlNodePtr node) {
        // Make a few xpath queries relative to the "D:response" calendar node (using "./")
        // to retrieve the attributes we're interested in.
        auto name = calendarSetDoc->nodeContentAtXPath(".//D:displayname/text()", node);
        auto path = calendarSetDoc->nodeContentAtXPath(".//D:href/text()", node);
        auto ctag = calendarSetDoc->nodeContentAtXPath(".//cs:getctag/text()", node);
        auto id = MailUtils::idForCalendar(account->id(), path);

        // Extract calendar metadata
        auto color = calendarSetDoc->nodeContentAtXPath(".//ical:calendar-color/text()", node);
        auto description = calendarSetDoc->nodeContentAtXPath(".//caldav:calendar-description/text()", node);
        auto orderStr = calendarSetDoc->nodeContentAtXPath(".//ical:calendar-order/text()", node);

        // Check for write privilege to determine read-only status
        // Look for <D:write/> in current-user-privilege-set
        auto privileges = calendarSetDoc->nodeContentAtXPath(".//D:current-user-privilege-set", node);
        bool readOnly = (privileges.find("<write") == string::npos && privileges.find(":write") == string::npos);

        shared_ptr<Calendar> calendar = local[id];
        bool needsSync = true;
        bool metadataChanged = false;

        // upsert the Calendar object
        if (calendar) {
            // Existing calendar - check if ctag changed
            if (calendar->ctag() == ctag && ctag != "") {
                logger->info("Calendar '{}' unchanged (ctag: {}), skipping sync", name, ctag);
                needsSync = false;
            }
            // Check if metadata changed
            if (calendar->name() != name) {
                calendar->setName(name);
                metadataChanged = true;
            }
            if (color != "" && calendar->color() != color) {
                calendar->setColor(color);
                metadataChanged = true;
            }
            if (calendar->description() != description) {
                calendar->setDescription(description);
                metadataChanged = true;
            }
            if (calendar->readOnly() != readOnly) {
                calendar->setReadOnly(readOnly);
                metadataChanged = true;
            }
            if (!orderStr.empty()) {
                int order = std::stoi(orderStr);
                if (calendar->order() != order) {
                    calendar->setOrder(order);
                    metadataChanged = true;
                }
            }
            if (metadataChanged) {
                store->save(calendar.get());
            }
        } else {
            // New calendar
            calendar = make_shared<Calendar>(id, account->id());
            calendar->setPath(path);
            calendar->setName(name);
            if (color != "") {
                calendar->setColor(color);
            }
            calendar->setDescription(description);
            calendar->setReadOnly(readOnly);
            if (!orderStr.empty()) {
                calendar->setOrder(std::stoi(orderStr));
            }
            store->save(calendar.get());
        }

        // sync if needed
        if (needsSync) {
            if (ctag != "") {
                logger->info("Syncing calendar '{}' (ctag: {} -> {})", name, calendar->ctag(), ctag);
            } else {
                logger->info("Syncing calendar '{}' (server doesn't provide ctag)", name);
            }

            // Try sync-token first (RFC 6578), fall back to legacy etag-based sync
            bool usedSyncToken = runForCalendarWithSyncToken(id, calHost + path, calendar);
            if (!usedSyncToken) {
                runForCalendar(id, name, calHost + path);
            }

            // Update ctag after successful sync
            if (ctag != "" && calendar->ctag() != ctag) {
                calendar->setCtag(ctag);
                store->save(calendar.get());
            }
        }
    }));
}

void DAVWorker::runForCalendar(string calendarId, string name, string url) {
    // Get time range for filtering events (RFC 4791 section 7.8)
    auto range = getCalendarSyncRange();

    // Remote: href -> etag (from server)
    map<string, string> remote {};
    {
        // Request events within the time range. The server expands recurring events
        // and returns any event where at least one instance falls within the range.
        string query =
            "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
            "<d:prop><d:getetag /></d:prop>"
            "<c:filter>"
            "<c:comp-filter name=\"VCALENDAR\">"
            "<c:comp-filter name=\"VEVENT\">"
            "<c:time-range start=\"" + range.startStr + "\" end=\"" + range.endStr + "\"/>"
            "</c:comp-filter>"
            "</c:comp-filter>"
            "</c:filter>"
            "</c:calendar-query>";

        auto eventEtagsDoc = performXMLRequest(url, "REPORT", query);

        eventEtagsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            auto etag = eventEtagsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
            auto href = eventEtagsDoc->nodeContentAtXPath(".//D:href/text()", node);
            remote[normalizeHref(href)] = string(etag.c_str());
        }));
    }

    // Local: href -> (icsUID, etag) - for comparison and stable ID lookup
    map<string, pair<string, string>> local {};
    {
        SQLite::Statement findEvents(store->db(), "SELECT data, icsuid, etag FROM Event WHERE calendarId = ?");
        findEvents.bind(1, calendarId);
        while (findEvents.executeStep()) {
            string dataStr = findEvents.getColumn("data").getString();
            json data = json::parse(dataStr);
            string href = data.count("href") ? data["href"].get<string>() : "";
            string icsuid = findEvents.getColumn("icsuid").getString();
            string etag = findEvents.getColumn("etag").getString();
            if (href != "") {
                local[normalizeHref(href)] = make_pair(icsuid, etag);
            }
        }
    }

    // Identify new, updated, and deleted events by comparing hrefs
    vector<string> deletedIcsUIDs {};
    vector<string> neededHrefs {};
    for (auto & pair : remote) {
        string href = pair.first;
        string remoteEtag = pair.second;
        auto it = local.find(href);
        if (it == local.end()) {
            // New event - need to fetch
            neededHrefs.push_back(href);
            continue;
        }
        if (it->second.second != remoteEtag) {
            // Updated event - etag changed, need to re-fetch
            neededHrefs.push_back(href);
        }
    }
    for (auto & pair : local) {
        if (remote.count(pair.first) == 0) {
            // Not in server response - either deleted or outside time range
            deletedIcsUIDs.push_back(pair.second.first);
        }
    }

    // Request newest events first
    std::reverse(neededHrefs.begin(), neededHrefs.end());
    if (!deletedIcsUIDs.empty() || !neededHrefs.empty()) {
        logger->info("{} (time range: {} to {})", url, range.startStr, range.endStr);
        logger->info("  remote: {} events in range", remote.size());
        logger->info("   local: {} events", local.size());
        logger->info(" removed: {} (deleted or out of range)", deletedIcsUIDs.size());
        logger->info("  needed: {}", neededHrefs.size());
    }

    auto deletionChunks = MailUtils::chunksOfVector(deletedIcsUIDs, 100);

    for (auto chunk : MailUtils::chunksOfVector(neededHrefs, 90)) {
        string payload = "";
        for (auto & href : chunk) {
            payload += "<D:href>" + href + "</D:href>";
        }

        // Fetch calendar data using REPORT calendar-multiget (RFC 4791 section 7.9).
        //
        // Important: We always use multiget instead of direct GET requests. This is a deliberate
        // design decision that ensures compatibility with servers like Zimbra that have broken
        // GET implementations. Zimbra returns 404 errors for direct GET requests to valid ICS
        // URLs, but the same URLs work correctly when requested via multiget REPORT.
        //
        // The Python caldav library implements a fallback strategy (try GET, fallback to multiget),
        // but we skip straight to multiget for reliability. The multiget approach also has
        // performance benefits by fetching multiple items in a single HTTP request.
        //
        // See: python-caldav compatibility hints for "event_by_url_is_broken" flag.
        auto icsDoc = performXMLRequest(url, "REPORT", "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag /><c:calendar-data /></d:prop>" + payload + "</c:calendar-multiget>");

        // Insert/update events and remove deleted events within the same transaction.
        {
            MailStoreTransaction transaction {store, "runForCalendar:insertions"};

            icsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                auto etag = icsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                auto icsData = icsDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);

                // Skip empty responses (Google sometimes returns empty data)
                if (etag == "" || icsData == "") {
                    if (etag != "") {
                        logger->info("Received calendar event {} with an empty body", etag);
                    }
                    return;
                }

                ICalendar cal(icsData);
                if (cal.Events.empty()) return;

                auto href = icsDoc->nodeContentAtXPath(".//D:href/text()", node);

                // Process ALL VEVENTs in the ICS file (master + any recurrence exceptions)
                for (auto icsEvent : cal.Events) {
                    if (icsEvent->DtStart.IsEmpty()) {
                        logger->info("Received calendar event but it has no start time?\n\n{}\n\n", icsData);
                        continue;
                    }

                    string icsUID = icsEvent->UID;
                    string recurrenceId = icsEvent->RecurrenceId;

                    // Look up existing event by icsUID + recurrenceId to update in place
                    // Master events have empty recurrenceId, exceptions have the occurrence date
                    auto query = Query().equal("calendarId", calendarId).equal("icsuid", icsUID);
                    if (!recurrenceId.empty()) {
                        query.equal("recurrenceId", recurrenceId);
                    } else {
                        query.equal("recurrenceId", "");
                    }
                    auto existing = store->find<Event>(query);

                    if (existing) {
                        // Update existing event - preserves stable ID
                        existing->setEtag(etag);
                        existing->setHref(href);
                        existing->setIcsData(icsData);
                        existing->setRecurrenceId(recurrenceId);
                        existing->setStatus(icsEvent->Status.empty() ? "CONFIRMED" : icsEvent->Status);
                        existing->_data["rs"] = icsEvent->DtStart.toUnix();
                        existing->_data["re"] = endOf(icsEvent).toUnix();
                        store->save(existing.get());
                    } else {
                        // Create new event - ID based on icsUID + recurrenceId
                        auto event = Event(etag, account->id(), calendarId, icsData, icsEvent);
                        event.setHref(href);
                        store->save(&event);
                    }
                }
            }));

            if (!deletionChunks.empty()) {
                auto deletionChunk = deletionChunks.back();
                deletionChunks.pop_back();
                auto deletionEvents = store->findAll<Event>(Query().equal("calendarId", calendarId).equal("icsuid", deletionChunk));
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
            auto deletionEvents = store->findAll<Event>(Query().equal("calendarId", calendarId).equal("icsuid", deletionChunk));
            for (auto & e : deletionEvents) {
                store->remove(e.get());
            }
        }
        transaction.commit();
    }
}

bool DAVWorker::runForCalendarWithSyncToken(string calendarId, string url, shared_ptr<Calendar> calendar, int retryCount) {
    const int maxRetries = 1; // Allow one retry for token expiration

    string syncToken = calendar->syncToken();
    bool isInitialSync = syncToken.empty();

    // Accumulated data across pagination requests
    vector<string> neededHrefs;
    vector<string> deletedHrefs;
    map<string, pair<string, string>> directData; // href -> (etag, icsData)

    // RFC 6578 pagination: loop until we get all results
    // Server returns 507 status when results are truncated
    bool hasMorePages = true;
    int pageCount = 0;
    const int maxPages = 100; // Safety limit to prevent infinite loops

    while (hasMorePages && pageCount < maxPages) {
        pageCount++;

        // Build sync-collection request per RFC 6578
        string tokenElement = syncToken.empty() ? "<D:sync-token/>" : "<D:sync-token>" + syncToken + "</D:sync-token>";

        // For incremental sync (with token), request full data since changeset is expected to be small
        // For initial sync (empty token), request only etags, then use multiget for data in chunks
        string props = isInitialSync
            ? "<D:getetag/>"
            : "<D:getetag/><C:calendar-data/>";

        string query = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<D:sync-collection xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
            + tokenElement +
            "<D:sync-level>1</D:sync-level>"
            "<D:prop>" + props + "</D:prop>"
            "</D:sync-collection>";

        shared_ptr<DavXML> syncDoc;
        bool http507Received = false;
        try {
            // RFC 6578 requires Depth: 0 for sync-collection
            syncDoc = performXMLRequest(url, "REPORT", query, "0");
        } catch (SyncException & e) {
            // Check for HTTP 507 (Insufficient Storage) - some servers return this as HTTP status
            // instead of embedding it in the 207 Multi-Status response per RFC 6578
            if (e.key.find("507") != string::npos) {
                size_t returnedPos = e.debuginfo.find(" RETURNED ");
                if (returnedPos != string::npos) {
                    string responseBody = e.debuginfo.substr(returnedPos + 10);
                    try {
                        syncDoc = make_shared<DavXML>(responseBody, url);
                        http507Received = true;
                        logger->info("Received HTTP 507, parsing truncated response");
                    } catch (...) {
                        logger->warn("Failed to parse HTTP 507 response body");
                    }
                }
            }

            // If we successfully parsed HTTP 507 response, continue processing
            if (syncDoc) {
                // Fall through to normal processing below
            } else if (e.key.find("403") != string::npos ||
                       e.key.find("409") != string::npos ||
                       e.key.find("410") != string::npos ||
                       e.debuginfo.find("valid-sync-token") != string::npos) {
                // Token expiration - retry with empty token if we haven't exceeded retries
                if (syncToken.empty() || retryCount >= maxRetries) {
                    if (retryCount >= maxRetries) {
                        logger->warn("Max retries ({}) exceeded for calendar sync token, falling back to legacy sync", maxRetries);
                    }
                    logger->info("sync-collection not supported or failed, using legacy sync");
                    return false;
                }
                logger->info("Sync token expired for calendar, retrying with full sync (attempt {})", retryCount + 1);
                calendar->setSyncToken("");
                store->save(calendar.get());
                return runForCalendarWithSyncToken(calendarId, url, calendar, retryCount + 1);
            } else {
                // Server doesn't support sync-collection or other error
                logger->info("sync-collection not supported or failed, using legacy sync");
                return false;
            }
        }

        // Extract new sync-token from response
        string newSyncToken = syncDoc->nodeContentAtXPath("//D:sync-token/text()");

        // Check for 507 (Insufficient Storage) status indicating truncated results
        // RFC 6578 section 3.6: server returns 507 when it can't return all results
        // This can come either as HTTP 507 (handled above) or embedded in 207 response
        hasMorePages = http507Received;
        syncDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
            auto href = syncDoc->nodeContentAtXPath(".//D:href/text()", node);
            auto status = syncDoc->nodeContentAtXPath(".//D:status/text()", node);

            if (status.find("507") != string::npos) {
                // Truncated response - more pages available (embedded in 207 Multi-Status per RFC 6578)
                hasMorePages = true;
                logger->info("sync-collection page {} truncated (507 in response), continuing...", pageCount);
            } else if (status.find("404") != string::npos) {
                // Item was deleted on server (RFC 6578 section 3.5)
                deletedHrefs.push_back(href);
            } else {
                auto etag = syncDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                auto icsData = syncDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);

                if (icsData != "") {
                    // Have full data (incremental sync response)
                    directData[href] = make_pair(etag, icsData);
                } else if (etag != "") {
                    // Need to fetch data (initial sync response with etags only)
                    neededHrefs.push_back(href);
                }
            }
        }));

        // Update sync token for next iteration (or final storage)
        if (newSyncToken != "") {
            syncToken = newSyncToken;
        }
    }

    if (pageCount >= maxPages) {
        logger->warn("sync-collection hit max pages limit ({}), sync may be incomplete", maxPages);
    }

    logger->info("sync-collection complete ({} pages): {} direct, {} needed, {} deleted",
                 pageCount, directData.size(), neededHrefs.size(), deletedHrefs.size());

    // Process direct data (from incremental sync with full calendar-data)
    if (!directData.empty()) {
        MailStoreTransaction transaction{store, "syncToken:direct"};

        for (auto & pair : directData) {
            string href = pair.first;
            string icsData = pair.second.second;
            if (icsData == "") continue;

            ICalendar cal(icsData);
            if (cal.Events.empty()) continue;

            string etag = pair.second.first;

            // Process ALL VEVENTs in the ICS file (master + any recurrence exceptions)
            for (auto icsEvent : cal.Events) {
                if (icsEvent->DtStart.IsEmpty()) continue;

                string icsUID = icsEvent->UID;
                string recurrenceId = icsEvent->RecurrenceId;

                // Look up existing event by icsUID + recurrenceId to update in place (preserves stable ID)
                auto query = Query().equal("calendarId", calendarId).equal("icsuid", icsUID);
                if (!recurrenceId.empty()) {
                    query.equal("recurrenceId", recurrenceId);
                } else {
                    query.equal("recurrenceId", "");
                }
                auto existing = store->find<Event>(query);

                if (existing) {
                    existing->setEtag(etag);
                    existing->setHref(href);
                    existing->setIcsData(icsData);
                    existing->setRecurrenceId(recurrenceId);
                    existing->setStatus(icsEvent->Status.empty() ? "CONFIRMED" : icsEvent->Status);
                    existing->_data["rs"] = icsEvent->DtStart.toUnix();
                    existing->_data["re"] = endOf(icsEvent).toUnix();
                    store->save(existing.get());
                } else {
                    // New event from sync-token - only create if within our time range
                    // This prevents accumulating events outside the range when they're modified
                    auto range = getCalendarSyncRange();
                    time_t eventStart = icsEvent->DtStart.toUnix();
                    time_t eventEnd = endOf(icsEvent).toUnix();

                    if (eventOverlapsRange(eventStart, eventEnd, range)) {
                        auto event = Event(etag, account->id(), calendarId, icsData, icsEvent);
                        event.setHref(href);
                        store->save(&event);
                    }
                    // else: quietly ignore - event is outside our sync window
                }
            }
        }
        transaction.commit();
    }

    // Fetch needed items (from initial sync) using multiget in chunks.
    // See comments in runForCalendar for why we use multiget instead of direct GET requests.
    if (!neededHrefs.empty()) {
        std::reverse(neededHrefs.begin(), neededHrefs.end());

        for (auto chunk : MailUtils::chunksOfVector(neededHrefs, 90)) {
            string payload = "";
            for (auto & icsHref : chunk) {
                payload += "<D:href>" + icsHref + "</D:href>";
            }

            auto icsDoc = performXMLRequest(url, "REPORT",
                "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                "<d:prop><d:getetag /><c:calendar-data /></d:prop>" + payload + "</c:calendar-multiget>");

            MailStoreTransaction transaction{store, "syncToken:multiget"};
            icsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
                auto etag = icsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
                auto icsData = icsDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);
                if (icsData == "" || etag == "") return;

                ICalendar cal(icsData);
                if (cal.Events.empty()) return;

                auto href = icsDoc->nodeContentAtXPath(".//D:href/text()", node);

                // Process ALL VEVENTs in the ICS file (master + any recurrence exceptions)
                for (auto icsEvent : cal.Events) {
                    if (icsEvent->DtStart.IsEmpty()) continue;

                    string icsUID = icsEvent->UID;
                    string recurrenceId = icsEvent->RecurrenceId;

                    // Look up existing event by icsUID + recurrenceId to update in place (preserves stable ID)
                    auto query = Query().equal("calendarId", calendarId).equal("icsuid", icsUID);
                    if (!recurrenceId.empty()) {
                        query.equal("recurrenceId", recurrenceId);
                    } else {
                        query.equal("recurrenceId", "");
                    }
                    auto existing = store->find<Event>(query);

                    if (existing) {
                        existing->setEtag(etag);
                        existing->setHref(href);
                        existing->setIcsData(icsData);
                        existing->setRecurrenceId(recurrenceId);
                        existing->setStatus(icsEvent->Status.empty() ? "CONFIRMED" : icsEvent->Status);
                        existing->_data["rs"] = icsEvent->DtStart.toUnix();
                        existing->_data["re"] = endOf(icsEvent).toUnix();
                        store->save(existing.get());
                    } else {
                        // New event - only create if within our time range
                        auto range = getCalendarSyncRange();
                        time_t eventStart = icsEvent->DtStart.toUnix();
                        time_t eventEnd = endOf(icsEvent).toUnix();

                        if (eventOverlapsRange(eventStart, eventEnd, range)) {
                            auto event = Event(etag, account->id(), calendarId, icsData, icsEvent);
                            event.setHref(href);
                            store->save(&event);
                        }
                    }
                }
            }));
            transaction.commit();
        }
    }

    // Delete removed items - load all events once, then find matches
    if (!deletedHrefs.empty()) {
        MailStoreTransaction transaction{store, "syncToken:deletions"};

        // Load all events from this calendar once (href is in JSON data column, can't query directly)
        auto allEvents = store->findAll<Event>(Query().equal("calendarId", calendarId));

        // Build a set of normalized hrefs for O(1) lookup
        set<string> deletedHrefSet;
        for (auto & href : deletedHrefs) {
            deletedHrefSet.insert(normalizeHref(href));
        }

        // Track master event icsUIDs for cascade deletion of exceptions
        set<string> deletedMasterUIDs;

        // Find and delete matching events
        for (auto & e : allEvents) {
            string eventHref = normalizeHref(e->href());
            if (deletedHrefSet.count(eventHref)) {
                // If this is a master event (no recurrenceId), track its icsUID
                // so we can cascade delete any exceptions
                if (e->recurrenceId().empty()) {
                    deletedMasterUIDs.insert(e->icsUID());
                }
                store->remove(e.get());
            }
        }

        // Cascade delete: remove any exceptions whose master was deleted
        // (exceptions may be in separate ICS files with different hrefs)
        if (!deletedMasterUIDs.empty()) {
            for (auto & e : allEvents) {
                // Skip if already deleted (href matched above)
                if (deletedHrefSet.count(normalizeHref(e->href()))) continue;

                // If this is an exception and its master was deleted, delete it too
                if (!e->recurrenceId().empty() && deletedMasterUIDs.count(e->icsUID())) {
                    store->remove(e.get());
                }
            }
        }

        transaction.commit();
    }

    // Store final sync-token for next sync
    if (syncToken != "" && syncToken != calendar->syncToken()) {
        calendar->setSyncToken(syncToken);
        store->save(calendar.get());
        logger->info("Stored new sync-token for calendar");
    }

    return true;
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

shared_ptr<DavXML> DAVWorker::performXMLRequest(string _url, string method, string payload, string depth) {
    // Apply rate limiting delay before making request (RFC 6585/7231 compliance)
    applyRateLimitDelay();

    string url = _url.find("http") != 0 ? "https://" + _url : _url;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, getAuthorizationHeader().c_str());
    headers = curl_slist_append(headers, "Prefer: return-minimal");
    headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");

    // Check for CardDAV namespace to set vCard Accept header
    if (payload.find("urn:ietf:params:xml:ns:carddav") != string::npos) {
        headers = curl_slist_append(headers, "Accept: text/vcard; version=4.0");
    }
    string depthHeader = "Depth: " + depth;
    headers = curl_slist_append(headers, depthHeader.c_str());

    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = payload.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 40);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);

    // Capture response headers for Retry-After extraction
    capturedHeaders.clear();
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, headerCallback);

    try {
        string result = PerformRequest(curl_handle);
        recordRequestSuccess();
        return make_shared<DavXML>(result, url);
    } catch (const SyncException& e) {
        // Check if this is a rate limit response (429 or 503)
        // The exception key format is "Invalid Response Code: XXX"
        if (e.key.find("429") != string::npos || e.key.find("503") != string::npos) {
            int httpCode = (e.key.find("429") != string::npos) ? 429 : 503;
            string retryAfter = extractHeader(capturedHeaders, "Retry-After");
            recordRateLimitResponse(httpCode, retryAfter);
        }
        throw;
    }
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

string DAVWorker::performICSRequest(string _url, string method, string icsData, ETAG existingEtag) {
    string url = _url.find("http") != 0 ? "https://" + _url : _url;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, getAuthorizationHeader().c_str());
    headers = curl_slist_append(headers, "Content-Type: text/calendar; charset=utf-8");

    if (existingEtag != "") {
        // For updates, use If-Match to ensure we don't overwrite concurrent changes
        string etagHeader = "If-Match: " + existingEtag;
        headers = curl_slist_append(headers, etagHeader.c_str());
    }

    CURL * curl_handle = curl_easy_init();
    const char * payloadChars = icsData.c_str();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 40);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, payloadChars);

    logger->info("performICSRequest: {} {} etag:{}", method, _url, existingEtag);
    string result = PerformRequest(curl_handle);
    return result;
}

void DAVWorker::writeAndResyncEvent(shared_ptr<Event> event) {
    // 1. Find the calendar to get its URL
    auto calendar = store->find<Calendar>(Query().equal("id", event->calendarId()));
    if (!calendar) {
        throw SyncException("no-calendar", "Calendar not found for event syncback", false);
    }

    string calendarUrl = calHost + calendar->path();
    string icsData = event->icsData();
    string href = event->href();
    string existingEtag = event->etag();

    // 2. Determine the href for the event
    if (href == "") {
        // No href stored - either a new event or a legacy event synced before we stored hrefs.
        // Generate the href from the UID (most CalDAV servers use {uid}.ics as the resource name)
        string uid = event->icsUID();
        if (uid == "") {
            uid = MailUtils::idRandomlyGenerated();
        }
        href = calendar->path() + uid + ".ics";
    }

    string fullUrl = replacePath(calendarUrl, href);

    // 3. Perform PUT request (create or update)
    try {
        performICSRequest(fullUrl, "PUT", icsData, existingEtag);
    } catch (SyncException & e) {
        if (e.key.find("412") != string::npos) {
            // Precondition failed - etag conflict, throw a more descriptive error
            throw SyncException("etag-conflict", "Event was modified by another client. Please refresh and try again.", false);
        }
        throw;
    }

    // 4. Read back the event to get the server's version (with new etag)
    // We use multiget REPORT instead of a direct GET request for Zimbra compatibility.
    // See comments in runForCalendar for details on the "event_by_url_is_broken" provider quirk.
    auto icsDoc = performXMLRequest(calendarUrl, "REPORT",
        "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
        "<d:prop><d:getetag /><c:calendar-data /></d:prop>"
        "<d:href>" + href + "</d:href>"
        "</c:calendar-multiget>");

    // 5. Parse response and update local event
    // Note: Event ID is now based on icsUID + recurrenceId (stable), so no ID regeneration is needed
    icsDoc->evaluateXPath("//D:response", ([&](xmlNodePtr node) {
        auto newEtag = icsDoc->nodeContentAtXPath(".//D:getetag/text()", node);
        auto icsResponse = icsDoc->nodeContentAtXPath(".//caldav:calendar-data/text()", node);
        auto hrefResponse = icsDoc->nodeContentAtXPath(".//D:href/text()", node);

        if (icsResponse != "" && newEtag != "") {
            ICalendar cal(icsResponse);
            if (!cal.Events.empty()) {
                // Find the VEVENT that matches the event we're syncing (by recurrenceId)
                // Server response may contain master + exceptions in one ICS file
                string eventRecurrenceId = event->recurrenceId();
                ICalendarEvent* matchingEvent = nullptr;

                for (auto icsEvent : cal.Events) {
                    if (icsEvent->RecurrenceId == eventRecurrenceId) {
                        matchingEvent = icsEvent;
                        break;
                    }
                }

                // Fall back to first event if no match (e.g., new master event)
                if (!matchingEvent) {
                    matchingEvent = cal.Events.front();
                }

                // Update local event with server data
                event->setEtag(newEtag);
                event->setHref(hrefResponse);
                event->setIcsData(icsResponse);
                event->setRecurrenceId(matchingEvent->RecurrenceId);
                event->setStatus(matchingEvent->Status.empty() ? "CONFIRMED" : matchingEvent->Status);

                // Update recurrence times from parsed ICS
                event->_data["rs"] = matchingEvent->DtStart.toUnix();
                event->_data["re"] = endOf(matchingEvent).toUnix();
                event->_data["icsuid"] = matchingEvent->UID;

                store->save(event.get());
                logger->info("Event syncback complete. New etag: {}", newEtag);
            }
        } else {
            logger->warn("Event syncback: received empty response from server");
        }
    }));
}

void DAVWorker::deleteEvent(shared_ptr<Event> event) {
    // 1. Find the calendar to get its URL
    auto calendar = store->find<Calendar>(Query().equal("id", event->calendarId()));
    if (!calendar) {
        throw SyncException("no-calendar", "Calendar not found for event deletion", false);
    }

    string href = event->href();

    // 2. If no href stored, try to reconstruct from UID
    if (href == "") {
        string uid = event->icsUID();
        if (uid == "") {
            throw SyncException("no-href", "Cannot delete event without href or icsUID", false);
        }
        href = calendar->path() + uid + ".ics";
    }

    string fullUrl = replacePath(calHost + calendar->path(), href);

    // 3. Perform DELETE request with If-Match header if we have an etag
    string existingEtag = event->etag();
    performICSRequest(fullUrl, "DELETE", "", existingEtag);

    // 4. Remove from local database
    store->remove(event.get());
    logger->info("Event deleted successfully: {}", href);
}

