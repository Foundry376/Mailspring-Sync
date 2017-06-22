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
    _data["lastMessageTimestamp"] = msg.date();
    _data["firstMessageTimestamp"] = msg.date();
    _data["lastMessageSentTimestamp"] = msg.date(); // TODO
    _data["lastMessageReceivedTimestamp"] = msg.date(); // TODO
    if (gThreadId) {
        _data["gThrId"] = to_string(gThreadId);
    }
    
    // we'll update these below
    _data["unread"] = 0;
    _data["starred"] = 0;
    _data["folders"] = json::array();
    _data["labels"] = json::array();
    _data["participants"] = json::array();

    addMessage(&msg, allLabels);
}

Thread::Thread(SQLite::Statement & query) :
MailModel(query)
{
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

bool Thread::inAllMail() {
    return _data["inAllMail"].get<bool>();
}

string Thread::gThrId() {
    return _data["gThrId"].get<string>();
}

time_t Thread::lastMessageTimestamp() {
    return _data["lastMessageTimestamp"].get<time_t>();
}

time_t Thread::firstMessageTimestamp() {
    return _data["firstMessageTimestamp"].get<time_t>();
}

time_t Thread::lastMessageReceivedTimestamp() {
    return _data["lastMessageReceivedTimestamp"].get<time_t>();
}

time_t Thread::lastMessageSentTimestamp() {
    return _data["lastMessageSentTimestamp"].get<time_t>();
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

    if (msg->date() > lastMessageTimestamp()) {
        _data["lastMessageTimestamp"] = msg->date();
    }
    if (msg->date() > lastMessageSentTimestamp()) {
        _data["lastMessageSentTimestamp"] = msg->date();
    }
    
    // update our folder set + increment refcounts
    string msgFolderId = msg->folderId();
    bool found = false;
    for (auto& f : folders()) {
        if (f["id"].get<string>() == msgFolderId) {
            f["_refcount"] = f["_refcount"].get<int>() + 1;
            found = true;
        }
    }
    if (!found) {
        json f = msg->folder();
        f["_refcount"] = 1;
        folders().push_back(f);
    }
    
    // update our label set + increment refcounts
    for (auto& mlname : msg->folderImapXGMLabels()) {
        shared_ptr<Label> ml = MailUtils::labelForXGMLabelName(mlname, allLabels);
        if (ml == nullptr) {
            continue;
        }

        bool found = false;
        for (auto& l : labels()) {
            if (l["id"].get<string>() == ml->id()) {
                l["_refcount"] = l["_refcount"].get<int>() + 1;
                found = true;
            }
        }
        if (!found) {
            json l = ml->toJSON();
            l["_refcount"] = 1;
            labels().push_back(l);
        }
    }
    
    // InAllMail should be true always for non-gmail accounts,
    // only true for Gmail when thread is not solely in spam or trash.
    int notSpamOrTrash = 0;
    for (auto& l : labels()) {
        string role = l["role"].get<string>();
        if ((role != "spam") && (role != "trash")) {
            notSpamOrTrash ++;
        }
    }
    _data["inAllMail"] = notSpamOrTrash == labels().size();
    
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
    
    // update our folder set + decrement refcounts
    string msgFolderId = msg->folderId();
    for (auto & f : folders()) {
        if (f["id"].get<string>() == msgFolderId) {
            f["_refcount"] = f["_refcount"].get<int>() - 1;
            break;
        }
    }
    
    // update our label set + decrement refcounts
    for (auto& mlname : msg->folderImapXGMLabels()) {
        shared_ptr<Label> ml = MailUtils::labelForXGMLabelName(mlname, allLabels);
        if (ml == nullptr) {
            continue;
        }
        json nextLabels = json::array();
        for (auto & l : labels()) {
            if (l["id"].get<string>() == ml->id()) {
                int r = l["_refcount"].get<int>();
                if (r > 1) {
                    l["_refcount"] = r - 1;
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

void Thread::bindToQuery(SQLite::Statement & query) {
    MailModel::bindToQuery(query);
    query.bind(":unread", unread());
    query.bind(":starred", starred());
    query.bind(":subject", subject());
    query.bind(":inAllMail", inAllMail());
    query.bind(":gThrId", gThrId());
    query.bind(":lastMessageTimestamp", (double)lastMessageTimestamp());
    query.bind(":lastMessageSentTimestamp", (double)lastMessageSentTimestamp());
    query.bind(":lastMessageReceivedTimestamp", (double)lastMessageReceivedTimestamp());
    query.bind(":firstMessageTimestamp", (double)firstMessageTimestamp());
}

void Thread::writeAssociations(SQLite::Database & db) {
    SQLite::Statement removeFolders(db, "DELETE FROM ThreadCategory WHERE id = ?");
    removeFolders.bind(1, id());
    removeFolders.exec();

    vector<string> categoryIds{};
    for (const auto & f : folders()) {
        categoryIds.push_back(f["id"].get<string>());
    }
    for (const auto & f : labels()) {
        categoryIds.push_back(f["id"].get<string>());
    }

    string qmarks = MailUtils::qmarkSets(categoryIds.size(), 6);
    SQLite::Statement insertFolders(db, "INSERT INTO ThreadCategory (id, value, inAllMail, unread, lastMessageReceivedTimestamp, lastMessageSentTimestamp) VALUES " + qmarks);

    int i = 1;
    for (auto& catId : categoryIds) {
        insertFolders.bind(i++, id());
        insertFolders.bind(i++, catId);
        insertFolders.bind(i++, inAllMail());
        insertFolders.bind(i++, unread() > 0);
        insertFolders.bind(i++, (double)lastMessageSentTimestamp());
        insertFolders.bind(i++, (double)lastMessageReceivedTimestamp());
    }
    insertFolders.exec();
}

#pragma mark Private

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

