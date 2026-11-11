//
//  MicrosoftContactsWorker.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MicrosoftContactsWorker.hpp"
#include "JSONUtils.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "NetworkRequestUtils.hpp"
#include "SyncException.hpp"
#include "XOAuth2TokenManager.hpp"

#include <atomic>
#include <mutex>
#include <set>

// The contact properties stored in a row's `info`, which the client maps to its editor.
// https://learn.microsoft.com/en-us/graph/api/resources/contact
static const vector<string> CONTACT_FIELDS = {
    "id", "changeKey", "parentFolderId", "displayName", "givenName", "middleName", "surname", "title",
    "generation", "nickName", "initials", "fileAs", "emailAddresses", "businessPhones", "homePhones",
    "mobilePhone", "imAddresses", "jobTitle", "companyName", "department", "officeLocation", "profession",
    "manager", "assistantName", "spouseName", "children", "homeAddress", "businessAddress", "otherAddress",
    "businessHomePage", "birthday", "personalNotes", "categories", "yomiGivenName", "yomiSurname", "yomiCompanyName"};

static string selectedFields() {
    string select = "";
    for (const auto & field : CONTACT_FIELDS) {
        select += (select == "" ? "" : ",") + field;
    }
    return select;
}

// The fields Graph sets itself.
static const set<string> READ_ONLY_FIELDS = {"id", "changeKey", "parentFolderId"};

// Graph's delta pages hold 10 contacts unless asked for more.
static const string DELTA_PAGE_SIZE = "200";

// The folders' delta links, as a JSON object of folder id -> @odata.deltaLink.
static string deltaLinksKey(shared_ptr<Account> account) {
    return "graph-contacts-delta-" + account->id();
}

// A pass and a write both decide from the rows whether a Graph contact is new. Without this, a
// pass that reads a contact the write just created, before the write stores its Graph id, would
// insert it a second time.
static std::mutex rowsMtx;
// Rows a write stored; a pass that loaded its contacts at an older count loads them again.
static std::atomic<unsigned> contactWrites { 0 };

MicrosoftContactsWorker::MicrosoftContactsWorker(shared_ptr<Account> account) :
    store(make_unique<MailStore>()),
    logger(spdlog::get("logger")),
    account(account)
{
}

// Asked for per request: a pass can outlast the token it began with, which is renewed only near expiry.
std::function<string()> MicrosoftContactsWorker::tokenProvider() {
    return [this]() {
        return SharedXOAuth2TokenManager()->partsForAccount(account, OAuthTokenAudience::MicrosoftGraphContacts).accessToken;
    };
}

// The ContactBook is how the client knows the account's contacts sync.
shared_ptr<ContactBook> MicrosoftContactsWorker::findOrCreateBook() {
    auto book = store->find<ContactBook>(Query().equal("accountId", account->id()));
    if (!book) {
        book = make_shared<ContactBook>(account->id() + "-default", account->id());
    }
    if (book->source() != GRAPH_SYNC_SOURCE) {
        book->setSource(GRAPH_SYNC_SOURCE);
        store->save(book.get());
    }
    return book;
}

// The default Contacts folder, by its well-known name, and the folders /me/contactFolders lists in it.
vector<string> MicrosoftContactsWorker::listFolderIds() {
    string url = MicrosoftGraphURL() + "/me/contactFolders/contacts?$select=id";
    GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()());
    if (!resp.ok()) {
        ThrowForGraphResponse(url, resp);
    }
    string defaultId = JSONString(resp.body, "id");
    if (defaultId == "") {
        throw SyncException("invalid-graph-resp", url + " returned no folder id", true);
    }

    vector<string> ids {defaultId};
    set<string> listed {defaultId};
    PaginateGraphCollection(MicrosoftGraphURL() + "/me/contactFolders?$select=id", tokenProvider(), [&](json & page) {
        for (const auto & folder : page["value"]) {
            // $skip paging repeats a folder that shifted between page fetches.
            if (listed.insert(JSONString(folder, "id")).second) {
                ids.push_back(JSONString(folder, "id"));
            }
        }
    });
    return ids;
}

// Rows without a Graph id are contacts whose create hasn't reached Graph; passes leave them alone.
void MicrosoftContactsWorker::refreshContactsIfWritten(bool force) {
    unsigned writes = contactWrites;
    if (!force && writes == contactsGeneration) {
        return;
    }
    contacts.clear();
    for (auto & contact : store->findAll<Contact>(Query().equal("accountId", account->id()).equal("source", GRAPH_SYNC_SOURCE))) {
        string graphId = JSONString(contact->info(), "id");
        if (graphId != "") {
            contacts[graphId] = contact;
        }
    }
    contactsGeneration = writes;
}

void MicrosoftContactsWorker::run() {
    vector<string> folderIds = listFolderIds();
    // After the first request, so an account Graph refuses shows no address book.
    string bookId = findOrCreateBook()->id();

    json storedLinks = json::object();
    string stored = store->getKeyValue(deltaLinksKey(account));
    if (stored != "") {
        storedLinks = json::parse(stored, nullptr, false);
        if (!storedLinks.is_object()) {
            storedLinks = json::object();
        }
    }

    {
        lock_guard<mutex> rows(rowsMtx);
        refreshContactsIfWritten(true);
    }

    // Removals wait until every folder is read: a contact another client moved shows up as removed
    // from its old folder and changed in its new one, in whichever order the folders are read.
    json nextLinks = json::object();
    vector<pair<string, string>> removals {}; // Graph id, the folder it left
    for (const auto & folderId : folderIds) {
        syncFolder(folderId, JSONString(storedLinks, folderId), bookId, nextLinks, removals);
    }

    lock_guard<mutex> rows(rowsMtx);
    refreshContactsIfWritten();
    set<string> listed(folderIds.begin(), folderIds.end());
    set<string> removedIds {};
    for (auto & [graphId, contact] : contacts) {
        if (!listed.count(JSONString(contact->info(), "parentFolderId"))) {
            removedIds.insert(graphId);
        }
    }
    for (auto & [graphId, folderId] : removals) {
        auto it = contacts.find(graphId);
        if (it != contacts.end() && JSONString(it->second->info(), "parentFolderId") == folderId) {
            removedIds.insert(graphId);
        }
    }
    vector<shared_ptr<Contact>> removed {};
    for (auto & graphId : removedIds) {
        removed.push_back(contacts[graphId]);
        contacts.erase(graphId);
    }
    size_t removedCount = removed.size(); // chunksOfVector empties `removed`
    for (auto & chunk : MailUtils::chunksOfVector(removed, 100)) {
        MailStoreTransaction transaction{store.get(), "syncContacts:deletions"};
        for (auto & contact : chunk) {
            store->remove(contact.get());
        }
        transaction.commit();
    }
    store->saveKeyValue(deltaLinksKey(account), nextLinks.dump());
    logger->info("Microsoft contact sync complete: {} folders, {} contacts removed", folderIds.size(), removedCount);
}

// Reads the folder's changes since `storedLink`, or all of its contacts when there is none, and stores
// them. Contacts the folder no longer holds are added to `removals`; its next delta link to `nextLinks`.
void MicrosoftContactsWorker::syncFolder(const string & folderId, const string & storedLink, const string & bookId,
                                         json & nextLinks, vector<pair<string, string>> & removals) {
    string fullURL = MicrosoftGraphURL() + "/me/contactFolders/" + URLEscape(folderId) + "/contacts/delta?$select=" + selectedFields();
    bool full = storedLink == "";
    string url = full ? fullURL : storedLink;
    set<string> seen {};
    size_t changed = 0;

    while (true) {
        GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()(), "", "application/json",
                                                 {"Prefer: odata.maxpagesize=" + DELTA_PAGE_SIZE});
        // An expired delta link answers 410 Gone; the folder is read again in full.
        // https://learn.microsoft.com/en-us/graph/delta-query-overview#synchronization-reset
        if (resp.status == 410 && !full) {
            logger->info("- Contact folder's delta link expired, reading it in full: {}", GraphErrorDescription(resp));
            full = true;
            url = fullURL;
            continue;
        }
        if (!resp.ok()) {
            ThrowForGraphResponse(url, resp);
        }
        const json & page = resp.body;
        if (!page.count("value") || !page["value"].is_array()) {
            throw SyncException("invalid-graph-resp", url + " returned no value array", true);
        }

        {
            lock_guard<mutex> rows(rowsMtx);
            refreshContactsIfWritten();
            MailStoreTransaction transaction{store.get(), "syncContacts:changes"};
            for (const auto & remote : page["value"]) {
                string graphId = JSONString(remote, "id");
                if (graphId == "") {
                    continue;
                }
                if (remote.count("@removed")) {
                    removals.push_back({graphId, folderId});
                    continue;
                }
                seen.insert(graphId);
                auto & contact = contacts[graphId];
                if (contact == nullptr) {
                    contact = make_shared<Contact>(MailUtils::idRandomlyGenerated(), account->id(), "", CONTACT_MAX_REFS, GRAPH_SYNC_SOURCE);
                }
                contact->setBookId(bookId);
                applyRemoteContact(contact, remote);
                store->save(contact.get());
                changed++;
            }
            transaction.commit();
        }

        string next = JSONString(page, "@odata.nextLink");
        if (next != "") {
            url = next;
            continue;
        }
        string deltaLink = JSONString(page, "@odata.deltaLink");
        if (deltaLink == "") {
            throw SyncException("invalid-graph-resp", url + " returned neither a next nor a delta link", true);
        }
        nextLinks[folderId] = deltaLink;
        break;
    }

    // A full read lists every contact in the folder, so the rows it didn't list are gone.
    if (full) {
        lock_guard<mutex> rows(rowsMtx);
        refreshContactsIfWritten();
        for (auto & [graphId, contact] : contacts) {
            if (!seen.count(graphId) && JSONString(contact->info(), "parentFolderId") == folderId) {
                removals.push_back({graphId, folderId});
            }
        }
    }
    logger->info("- Contact folder {}: {} contacts changed{}", folderId.substr(folderId.length() > 12 ? folderId.length() - 12 : 0),
                 changed, full ? " (full read)" : "");
}

static string displayNameOf(const json & info) {
    string name = JSONString(info, "displayName");
    if (name == "") {
        name = JSONString(info, "givenName");
        string surname = JSONString(info, "surname");
        if (surname != "") {
            name += (name == "" ? "" : " ") + surname;
        }
    }
    return name;
}

static string primaryEmailOf(const json & info) {
    for (const auto & email : JSONArray(info, "emailAddresses")) {
        string address = JSONString(email, "address");
        if (address != "") {
            return address;
        }
    }
    return "";
}

void MicrosoftContactsWorker::applyRemoteContact(shared_ptr<Contact> contact, const json & remote) {
    // Graph's write responses carry more than a pass selects; rows hold the same fields either way.
    json info = json::object();
    for (const auto & field : CONTACT_FIELDS) {
        if (remote.count(field)) {
            info[field] = remote[field];
        }
    }
    contact->setInfo(info);
    contact->setEtag(JSONString(info, "changeKey"));
    contact->setName(displayNameOf(info));
    contact->setEmail(primaryEmailOf(info));
}

// The writable fields of `info`, for a create.
static json fieldsToCreate(const json & info) {
    json body = json::object();
    for (const auto & field : CONTACT_FIELDS) {
        if (!READ_ONLY_FIELDS.count(field) && info.count(field) && !info[field].is_null()) {
            body[field] = info[field];
        }
    }
    return body;
}

// The writable fields `info` changes from `previous`, so an edit leaves alone what another client
// changed since. A field `info` drops is cleared: a collection with [], an address with {} (Graph
// answers null there with a 500), anything else with null.
static json fieldsToUpdate(const json & previous, const json & info) {
    json body = json::object();
    for (const auto & field : CONTACT_FIELDS) {
        if (READ_ONLY_FIELDS.count(field)) {
            continue;
        }
        json before = previous.is_object() && previous.count(field) ? previous[field] : json();
        json after = info.count(field) ? info[field] : json();
        if (before == after) {
            continue;
        }
        body[field] = !after.is_null() ? after : before.is_array() ? json::array() : before.is_object() ? json::object() : json();
    }
    // Graph may regenerate displayName from the name fields an update changes, unless it is sent too.
    // https://learn.microsoft.com/en-us/graph/api/resources/contact (displayName)
    if (!body.empty() && !body.count("displayName") && info.count("displayName")) {
        body["displayName"] = info["displayName"];
    }
    return body;
}

void MicrosoftContactsWorker::upsertContact(shared_ptr<Contact> contact, const json & info, const json & previousInfo) {
    lock_guard<mutex> rows(rowsMtx);
    // A pass may have stored Graph's copy since the task loaded the row.
    auto row = store->find<Contact>(Query().equal("id", contact->id()));
    if (row == nullptr) {
        throw SyncException("not-found", "The contact was deleted before its changes reached Microsoft", false);
    }
    string graphId = JSONString(row->info(), "id");

    string url;
    GraphResponse resp;
    if (graphId == "") {
        url = MicrosoftGraphURL() + "/me/contacts";
        // Created only when throttled: after a 503 the contact may exist, and a second POST would duplicate it.
        resp = PerformGraphRequest("POST", url, tokenProvider()(), fieldsToCreate(info).dump(), "application/json", {}, GraphRetry::ThrottledOnly);
    } else {
        json changes = fieldsToUpdate(previousInfo, info);
        if (changes.empty()) {
            logger->info("- Contact {} has no changes to write", row->id());
            return;
        }
        url = MicrosoftGraphURL() + "/me/contacts/" + URLEscape(graphId);
        resp = PerformGraphRequest("PATCH", url, tokenProvider()(), changes.dump());
    }
    if (!resp.ok()) {
        // The row holds the refused edit, and no pass will mention a contact Graph didn't change.
        restoreContact(row, graphId);
        if (resp.status == 404) {
            throw SyncException("not-found", url + " RETURNED " + GraphErrorDescription(resp), false);
        }
        ThrowForGraphResponse(url, resp);
    }
    applyRemoteContact(row, resp.body);
    row->setBookId(findOrCreateBook()->id());
    store->save(row.get());
    contactWrites++;
}

// Puts Graph's copy of the contact back in its row, or removes a row Graph doesn't hold.
// Best effort: the caller is about to report the failure that brought it here.
void MicrosoftContactsWorker::restoreContact(shared_ptr<Contact> row, const string & graphId) {
    if (graphId == "") {
        store->remove(row.get());
        contactWrites++;
        return;
    }
    string url = MicrosoftGraphURL() + "/me/contacts/" + URLEscape(graphId) + "?$select=" + selectedFields();
    GraphResponse resp = PerformGraphRequest("GET", url, tokenProvider()());
    if (resp.status == 404) {
        store->remove(row.get());
    } else if (resp.ok()) {
        applyRemoteContact(row, resp.body);
        store->save(row.get());
    } else {
        logger->warn("-X Contact {} could not be read back: {}", row->id(), GraphErrorDescription(resp));
        return;
    }
    contactWrites++;
}

void MicrosoftContactsWorker::deleteContact(shared_ptr<Contact> contact) {
    lock_guard<mutex> rows(rowsMtx);
    string graphId = JSONString(contact->info(), "id");
    if (graphId != "") {
        string url = MicrosoftGraphURL() + "/me/contacts/" + URLEscape(graphId);
        GraphResponse resp = PerformGraphRequest("DELETE", url, tokenProvider()());
        // A 404 is a contact already deleted elsewhere, which is what the user asked for.
        if (!resp.ok() && resp.status != 404) {
            ThrowForGraphResponse(url, resp);
        }
    }
    store->remove(contact.get());
    contactWrites++;
}
