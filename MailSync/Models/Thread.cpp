//
//  Thread.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Thread.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "Placement.hpp"

#define DEFAULT_SUBJECT "unassigned"

using namespace std;

string Thread::TABLE_NAME = "Thread";

Thread::Thread(string msgId, string accountId, string subject, uint64_t gThreadId) :
    MailModel("t:" + msgId, accountId, 0)
{
    // set immutable properties of new Thread
    _data["subject"] = subject;
    _data["lmt"] = 0;
    _data["fmt"] = INT_MAX;
    _data["lmst"] = 0;
    _data["lmrt"] = 0;
    if (gThreadId) {
        _data["gThrId"] = to_string(gThreadId);
    } else {
        _data["gThrId"] = "";
    }
    
    // we'll update these below
    _data["unread"] = 0;
    _data["starred"] = 0;
    _data["inAllMail"] = false;
    _data["attachmentCount"] = 0;
    _data["searchRowId"] = 0;
    _data["folders"] = json::array();
    _data["labels"] = json::array();
    _data["participants"] = json::array();

    captureInitialState();
}

Thread::Thread(SQLite::Statement & query) :
MailModel(query)
{
    captureInitialState();
}

bool Thread::supportsMetadata() {
    return true;
}

string Thread::subject() {
    return _data["subject"].get<string>();
}

void Thread::setSubject(string s) {
    _data["subject"] = s;
}

int Thread::unread() {
    return _data["unread"].get<int>();
}

void Thread::setUnread(int u) {
    _data["unread"] = u;
}

int Thread::starred() {
    return _data["starred"].get<int>();
}

void Thread::setStarred(int s) {
    _data["starred"] = s;
}

int Thread::attachmentCount() {
    return _data["attachmentCount"].get<int>();
}

void Thread::setAttachmentCount(int s) {
    _data["attachmentCount"] = s;
}

uint64_t Thread::searchRowId() {
    return _data["searchRowId"].get<uint64_t>();
}

void Thread::setSearchRowId(uint64_t s) {
    _data["searchRowId"] = s;
}

bool Thread::inAllMail() {
    return _data["inAllMail"].get<bool>();
}

string Thread::gThrId() {
    return _data["gThrId"].get<string>();
}

time_t Thread::lastMessageTimestamp() {
    return _data["lmt"].get<time_t>();
}

time_t Thread::firstMessageTimestamp() {
    return _data["fmt"].get<time_t>();
}

time_t Thread::lastMessageReceivedTimestamp() {
    return _data["lmrt"].get<time_t>();
}

time_t Thread::lastMessageSentTimestamp() {
    return _data["lmst"].get<time_t>();
}

json & Thread::folders() {
    return _data["folders"];
}

json & Thread::labels() {
    return _data["labels"];
}

json & Thread::participants() {
    return _data["participants"];
}

string Thread::categoriesSearchString() {
    string categories;
    for (auto f : folders()) {
        string role = f.count("role") ? f["role"].get<string>() : "";
        if (role.length()) {
            categories += role + " ";
        } else {
            categories += f["path"].get<string>() + " ";
        }
    }
    for (auto f : labels()) {
        string role = f.count("role") ? f["role"].get<string>() : "";
        if (role.length()) {
            categories += role + " ";
        } else {
            categories += f["path"].get<string>() + " ";
        }
    }
    return categories;
}

void Thread::resetCountedAttributes() {
    setUnread(0);
    setStarred(0);
    setAttachmentCount(0);
    _data["folders"] = json::array();
    _data["labels"] = json::array();

    // now call applyMessageAttributeChanges(empty, msg) for all messages
}

static bool anyFolderOutsideSpamOrTrash(MailStore * store, string accountId, json & folderBits) {
    for (auto it = folderBits.begin(); it != folderBits.end(); ++it) {
        auto folder = store->folderById(accountId, it.key());
        string role = folder == nullptr ? "" : folder->role();
        if (role != "spam" && role != "trash") {
            return true;
        }
    }
    return false;
}

/*
 Updates the thread's counters and folder / label sets from a before + after view of one
 message. `old` is the message's state when it was loaded (or the empty snapshot for a
 new message); `next` is its current state, or null when it is being removed.

 Folder refcounts are per distinct folder: a message with two copies in one folder still
 contributes one reference. The per-folder unread count (`_u`, which feeds ThreadCategory
 and the ThreadCounts badge) uses that copy's own unread bit, so an unread Inbox copy
 counts for Inbox while the read Sent copy of the same message does not count for Sent.
 */
void Thread::applyMessageAttributeChanges(MessageSnapshot & old, Message * next, MailStore * store) {
    auto allLabels = store->allLabelsCache(accountId());

    // decrement basic attributes
    setUnread(unread() - old.unread);
    setStarred(starred() - old.starred);
    setAttachmentCount(attachmentCount() - (int)old.fileCount);

    // decrement folder + label refcounts.
    //
    // Note: Since labels are within `All Mail`, a message only contributes
    // to a label's unread count if it is also in `All Mail`.
    adjustFolderRefs(store, old.folders, -1);
    bool oldInAllMail = anyFolderOutsideSpamOrTrash(store, accountId(), old.folders);
    adjustLabelRefs(allLabels, old.labels, -1, old.unread && oldInAllMail);
    
    if (next) {
        // increment basic attributes
        setUnread(unread() + next->isUnread());
        setStarred(starred() + next->isStarred());
        setAttachmentCount(attachmentCount() + (int)next->fileCountForThreadList());

        // increment dates
        if (!next->isDraft() && !next->isDeletionPlaceholder()) {
            if (next->date() > lastMessageTimestamp()) {
                _data["lmt"] = next->date();
            }
            if (next->date() < firstMessageTimestamp()) {
                _data["fmt"] = next->date();
            }
            if (next->isSentByUser(store) && !next->isHiddenReminder()) {
                if (next->date() > lastMessageSentTimestamp()) {
                    _data["lmst"] = next->date();
                }
            }

            // Note: Emails you send yourself impact the `lmrt`, so saying
            // "not sent by me" is not sufficient. TODO: better logic?
            if (next->isInInbox(store) || !next->isSentByUser(store)) {
                if (_data.count("lmrt_is_fallback") || next->date() > lastMessageReceivedTimestamp()) {
                    _data.erase("lmrt_is_fallback");
                    _data["lmrt"] = next->date();
                }
            } else if (lastMessageReceivedTimestamp() == 0) {
                // This message should not be used for "last message received", but we
                // never ever want the value to be zero. It causes the thread to appear
                // at the bottom of a label view and show "1969" in search results, etc.
                
                // Use this value until we find one that does meet our criteria (may be added
                // later in sync since we scan mail from new to old.)
                _data["lmrt_is_fallback"] = true;
                _data["lmrt"] = next->date();
            }
        }

        // increment folder + label refcounts
        adjustFolderRefs(store, next->folders(), +1);
        adjustLabelRefs(allLabels, next->remoteXGMLabels(), +1, next->isUnread() && next->inAllMail(store));
        
        // merge in participants
        std::map<std::string, bool>emails;
        for (auto& p : participants()) {
            if (p.count("email")) {
                emails[p["email"].get<string>()] = true;
            }
        }
        addMissingParticipants(emails, next->to());
        addMissingParticipants(emails, next->cc());
        addMissingParticipants(emails, next->from());
    }

    // InAllMail should be true unless the thread is entirely in the spam or trash folder.
    int spamOrTrash = 0;
    for (auto& f : folders()) {
        string role = f["role"].get<string>();
        if ((role == "spam") || (role == "trash")) {
            spamOrTrash ++;
        }
    }
    _data["inAllMail"] = folders().size() > spamOrTrash;
}

// Applies delta (+1 / -1) to the refcount of every folder in a { folderId: bits } map,
// adding the folder's JSON (from the store cache) on first reference and dropping the
// entry when its refcount reaches zero. `_u` moves by the copy's own unread bit.
void Thread::adjustFolderRefs(MailStore * store, json & folderBits, int delta) {
    for (auto it = folderBits.begin(); it != folderBits.end(); ++it) {
        string folderId = it.key();
        int bits = it.value().is_number() ? it.value().get<int>() : 0;
        int unreadBit = (bits & PLACEMENT_FLAG_UNREAD) ? 1 : 0;

        bool found = false;
        json nextFolders = json::array();
        for (auto & f : folders()) {
            if (f["id"].get<string>() != folderId) {
                nextFolders.push_back(f);
                continue;
            }
            found = true;
            int r = f["_refs"].get<int>() + delta;
            if (r > 0) {
                f["_refs"] = r;
                f["_u"] = f["_u"].get<int>() + delta * unreadBit;
                nextFolders.push_back(f);
            }
        }
        if (!found && delta > 0) {
            auto folder = store->folderById(accountId(), folderId);
            json f;
            if (folder != nullptr) {
                f = folder->toJSON();
                f.erase("localStatus");
            } else {
                // The folder row is gone but a placement still names it; keep the shape
                // categoriesSearchString and the inAllMail loop expect.
                f = {{"id", folderId}, {"aid", accountId()}, {"path", ""}, {"role", ""}, {"__cls", Folder::TABLE_NAME}};
            }
            f["_refs"] = 1;
            f["_u"] = unreadBit;
            nextFolders.push_back(f);
        }
        _data["folders"] = nextFolders;
    }
}

void Thread::adjustLabelRefs(vector<shared_ptr<Label>> & allLabels, json & labelNames, int delta, bool unreadInAllMail) {
    int unreadBit = unreadInAllMail ? 1 : 0;

    for (auto & mlname : labelNames) {
        shared_ptr<Label> ml = MailUtils::labelForXGMLabelName(mlname, allLabels);
        if (ml == nullptr) {
            continue;
        }

        bool found = false;
        json nextLabels = json::array();
        for (auto & l : labels()) {
            if (l["id"].get<string>() != ml->id()) {
                nextLabels.push_back(l);
                continue;
            }
            found = true;
            int r = l["_refs"].get<int>() + delta;
            if (r > 0) {
                l["_refs"] = r;
                l["_u"] = l["_u"].get<int>() + delta * unreadBit;
                nextLabels.push_back(l);
            }
        }
        if (!found && delta > 0) {
            json l = ml->toJSON();
            l["_refs"] = 1;
            l["_u"] = unreadBit;
            nextLabels.push_back(l);
        }
        _data["labels"] = nextLabels;
    }
}

string Thread::tableName() {
    return Thread::TABLE_NAME;
}

vector<string> Thread::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "gThrId", "unread", "starred", "inAllMail", "subject", "lastMessageTimestamp", "lastMessageReceivedTimestamp", "lastMessageSentTimestamp", "firstMessageTimestamp", "hasAttachments"};
}

void Thread::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":unread", unread());
    query->bind(":starred", starred());
    query->bind(":subject", subject());
    query->bind(":inAllMail", inAllMail());
    query->bind(":gThrId", gThrId());
    query->bind(":lastMessageTimestamp", (double)lastMessageTimestamp());
    query->bind(":lastMessageSentTimestamp", (double)lastMessageSentTimestamp());
    query->bind(":lastMessageReceivedTimestamp", (double)lastMessageReceivedTimestamp());
    query->bind(":firstMessageTimestamp", (double)firstMessageTimestamp());
    query->bind(":hasAttachments", (double)attachmentCount());
}

void Thread::afterSave(MailStore * store) {
    MailModel::afterSave(store);
    
    bool _inAllMail = inAllMail();
    double _lmrt = (double)lastMessageReceivedTimestamp();
    double _lmst = (double)lastMessageSentTimestamp();
    map<string, bool> categoryIds = captureCategoryIDs();

    // update the ThreadCategory join table to include our folder and labels
    // note this is pretty expensive, so we avoid it if relevant attributes
    // have not changed since the model was loaded.
    if (_initialCategoryIds != categoryIds || _initialLMRT != _lmrt || _initialLMST != _lmst) {
        string _id = id();
        SQLite::Statement removeFolders(store->db(), "DELETE FROM ThreadCategory WHERE id = ?");
        removeFolders.bind(1, id());
        removeFolders.exec();

        if (categoryIds.size() > 0) {
            SQLite::Statement insertFolders(store->db(), "INSERT INTO ThreadCategory (id, value, inAllMail, unread, lastMessageReceivedTimestamp, lastMessageSentTimestamp) VALUES (?,?,?,?,?,?)");
            for (auto& it : categoryIds) {
                insertFolders.bind(1, _id);
                insertFolders.bind(2, it.first);
                insertFolders.bind(3, _inAllMail);
                insertFolders.bind(4, it.second);
                insertFolders.bind(5, _lmrt);
                insertFolders.bind(6, _lmst);
                insertFolders.exec();
                insertFolders.reset();
            }
        }
    }

    if (_initialCategoryIds != categoryIds) {
        // update the thread counts table. We keep track of our initial / updated
        // unread count and category membership so that we can quickly compute changes
        // to these counters
        map<string, array<int, 2>> diffs{};
        for (auto& it : _initialCategoryIds) {
            diffs[it.first] = {-it.second, -1};
        }
        for (auto& it : categoryIds) {
            if (diffs.count(it.first)) {
                diffs[it.first] = {diffs[it.first][0] + it.second, -1 + 1};
            } else {
                diffs[it.first] = {it.second, 1};
            }
        }
        SQLite::Statement changeCounters(store->db(), "UPDATE ThreadCounts SET unread = unread + ?, total = total + ? WHERE categoryId = ?");

        for (auto& it : diffs) {
            if (it.second[0] == 0 && it.second[1] == 0) {
                continue;
            }
            changeCounters.bind(1, it.second[0]);
            changeCounters.bind(2, it.second[1]);
            changeCounters.bind(3, it.first);
            changeCounters.exec();
            changeCounters.reset();
        }

        // update the thread search table if we're indexed
        if (searchRowId()) {
            SQLite::Statement update(store->db(), "UPDATE ThreadSearch SET categories = ? WHERE rowid = ?");
            update.bind(1, categoriesSearchString());
            update.bind(2, (double)searchRowId());
            update.exec();
        }
    }
}

void Thread::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);
    
    // ensure ThreadCounts and ThreadCategories reflect our state. Since
    // all messages should already have been removed from the thread,
    // this will remove everything.
    afterSave(store);

    // Delete search entry
    if (searchRowId()) {
        SQLite::Statement update(store->db(), "DELETE FROM ThreadSearch WHERE rowid = ?");
        update.bind(1, (double)searchRowId());
        update.exec();
    }
}


#pragma mark Private

map<string, bool> Thread::captureCategoryIDs() {
    map<string, bool> result{};
    for (const auto & f : folders()) {
        result[f["id"].get<string>()] = f["_u"].get<int>() > 0;
    }
    for (const auto & l : labels()) {
        result[l["id"].get<string>()] = l["_u"].get<int>() > 0;
    }
    return result;
}

void Thread::captureInitialState() {
    _initialLMST = lastMessageSentTimestamp();
    _initialLMRT = lastMessageReceivedTimestamp();
    _initialCategoryIds = captureCategoryIDs();
}

void Thread::addMissingParticipants(std::map<std::string, bool> & existing, json & incoming) {
    for (const auto & contact : incoming) {
        if (contact.count("email")) {
            const auto email = contact["email"].get<string>();
            if (!existing.count(email)) {
                existing[email] = true;
                _data["participants"] += contact;
            }
        }
    }
}

