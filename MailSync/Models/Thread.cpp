//
//  Thread.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Thread.hpp"
#include "MailUtils.hpp"

#define DEFAULT_SUBJECT "unassigned"

using namespace std;

string Thread::TABLE_NAME = "Thread";

Thread::Thread(Message msg, uint64_t gThreadId, vector<shared_ptr<Label>> & allLabels) :
    MailModel("t:" + msg.id(), msg.accountId(), 0)
{
    // set immutable properties of new Thread
    _data["subject"] = msg.subject();
    _data["lmt"] = msg.date();
    _data["fmt"] = msg.date();
    _data["lmst"] = msg.date();
    _data["lmrt"] = msg.date();
    if (gThreadId) {
        _data["gThrId"] = to_string(gThreadId);
    } else {
        _data["gThrId"] = "";
    }
    
    // we'll update these below
    _data["unread"] = 0;
    _data["starred"] = 0;
    _data["attachmentCount"] = 0;
    _data["searchRowId"] = 0;
    _data["folders"] = json::array();
    _data["labels"] = json::array();
    _data["participants"] = json::array();

    captureInitialState();
    addMessage(&msg, allLabels);
}

Thread::Thread(SQLite::Statement & query) :
MailModel(query)
{
    captureInitialState();
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

void Thread::addMessage(Message * msg, vector<shared_ptr<Label>> & allLabels) {
    // update basic attributes
    setUnread(unread() + msg->isUnread());
    setStarred(starred() + msg->isStarred());
    setAttachmentCount(attachmentCount() + (int)msg->files().size());
    
    if (msg->date() > lastMessageTimestamp()) {
        _data["lmt"] = msg->date();
    }
    if (msg->date() < firstMessageTimestamp()) {
        _data["fmt"] = msg->date();
    }
    if (msg->isSentByUser()) {
        if (msg->date() > lastMessageSentTimestamp()) {
            _data["lmst"] = msg->date();
        }
    } else {
        if (msg->date() > lastMessageReceivedTimestamp()) {
            _data["lmrt"] = msg->date();
        }
    }
    
    // update our folder set + increment refcounts
    string msgFolderId = msg->clientFolderId();
    bool found = false;
    for (auto& f : folders()) {
        if (f["id"].get<string>() == msgFolderId) {
            f["_refs"] = f["_refs"].get<int>() + 1;
            f["_u"] = f["_u"].get<int>() + msg->isUnread();
            found = true;
        }
    }
    if (!found) {
        json f = msg->clientFolder();
        f["_refs"] = 1;
        f["_u"] = msg->isUnread() ? 1 : 0;
        folders().push_back(f);
    }
    
    // update our label set + increment refcounts
    for (auto& mlname : msg->remoteXGMLabels()) {
        shared_ptr<Label> ml = MailUtils::labelForXGMLabelName(mlname, allLabels);
        if (ml == nullptr) {
            continue;
        }

        bool found = false;
        for (auto& l : labels()) {
            if (l["id"].get<string>() == ml->id()) {
                l["_refs"] = l["_refs"].get<int>() + 1;
                l["_u"] = l["_u"].get<int>() + msg->isUnread();
                found = true;
            }
        }
        if (!found) {
            json l = ml->toJSON();
            l["_refs"] = 1;
            l["_u"] = msg->isUnread() ? 1 : 0;
            labels().push_back(l);
        }
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
    
    // merge in participants
    std::map<std::string, bool>emails;
    for (auto& p : participants()) {
        if (p.count("email")) {
            emails[p["email"].get<string>()] = true;
        }
    }
    addMissingParticipants(emails, msg->to());
    addMissingParticipants(emails, msg->cc());
    addMissingParticipants(emails, msg->from());
}

void Thread::prepareToReaddMessage(Message * msg, vector<shared_ptr<Label>> & allLabels) {
    // update basic attributes
    setUnread(unread() - msg->isUnread());
    setStarred(starred() - msg->isStarred());
    setAttachmentCount(attachmentCount() - (int)msg->files().size());

    // update our folder set + decrement refcounts
    string msgFolderId = msg->clientFolderId();
    json nextFolders = json::array();
    for (auto & f : folders()) {
        if (f["id"].get<string>() == msgFolderId) {
            int r = f["_refs"].get<int>();
            if (r > 1) {
                f["_refs"] = r - 1;
                f["_u"] = f["_u"].get<int>() - msg->isUnread();
                nextFolders.push_back(f);
            }
            break;
        }
    }
    _data["folders"] = nextFolders;

    // update our label set + decrement refcounts
    for (auto& mlname : msg->remoteXGMLabels()) {
        shared_ptr<Label> ml = MailUtils::labelForXGMLabelName(mlname, allLabels);
        if (ml == nullptr) {
            continue;
        }
        json nextLabels = json::array();
        for (auto & l : labels()) {
            if (l["id"].get<string>() == ml->id()) {
                int r = l["_refs"].get<int>();
                if (r > 1) {
                    l["_refs"] = r - 1;
                    l["_u"] = l["_u"].get<int>() - msg->isUnread();
                    nextLabels.push_back(l);
                }
                break;
            }
        }
        _data["labels"] = nextLabels;
    }
}

string Thread::tableName() {
    return Thread::TABLE_NAME;
}

vector<string> Thread::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "gThrId", "unread", "starred", "inAllMail", "subject", "lastMessageTimestamp", "lastMessageReceivedTimestamp", "lastMessageSentTimestamp", "firstMessageTimestamp"};
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
}

void Thread::writeAssociations(SQLite::Database & db) {
    MailModel::writeAssociations(db);
    
    bool _inAllMail = inAllMail();
    double _lmrt = (double)lastMessageReceivedTimestamp();
    double _lmst = (double)lastMessageSentTimestamp();

    map<string, bool> categoryIds{};
    for (const auto & f : folders()) {
        categoryIds[f["id"].get<string>()] = f["_u"].get<int>() > 0;
    }
    for (const auto & l : labels()) {
        categoryIds[l["id"].get<string>()] = l["_u"].get<int>() > 0;
    }

    // update the ThreadCategory join table to include our folder and labels
    // note this is pretty expensive, so we avoid it if relevant attributes
    // have not changed since the model was loaded.
    if (_initialCategoryIds != categoryIds || _initialLMRT != _lmrt || _initialLMST != _lmst) {
        string _id = id();
        SQLite::Statement removeFolders(db, "DELETE FROM ThreadCategory WHERE id = ?");
        removeFolders.bind(1, id());
        removeFolders.exec();

        string qmarks = MailUtils::qmarkSets(categoryIds.size(), 6);
        
        SQLite::Statement insertFolders(db, "INSERT INTO ThreadCategory (id, value, inAllMail, unread, lastMessageReceivedTimestamp, lastMessageSentTimestamp) VALUES " + qmarks);

        int i = 1;
        for (auto& it : categoryIds) {
            insertFolders.bind(i++, _id);
            insertFolders.bind(i++, it.first);
            insertFolders.bind(i++, _inAllMail);
            insertFolders.bind(i++, it.second);
            insertFolders.bind(i++, _lmrt);
            insertFolders.bind(i++, _lmst);
        }
        insertFolders.exec();
    }

    // update the thread counts table. We keep track of our initial / updated
    // unread count and category membership so that we can quickly compute changes
    // to these counters
    if (_initialCategoryIds != categoryIds) {
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
        SQLite::Statement changeCounters(db, "UPDATE ThreadCounts SET unread = unread + ?, total = total + ? WHERE categoryId = ?");

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
    }
}

#pragma mark Private

void Thread::captureInitialState() {
    _initialLMST = lastMessageSentTimestamp();
    _initialLMRT = lastMessageReceivedTimestamp();
    for (const auto & f : folders()) {
        _initialCategoryIds[f["id"].get<string>()] = f["_u"].get<int>() > 0;
    }
    for (const auto & f : labels()) {
        _initialCategoryIds[f["id"].get<string>()] = f["_u"].get<int>() > 0;
    }
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

