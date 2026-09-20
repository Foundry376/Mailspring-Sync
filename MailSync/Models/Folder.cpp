//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Folder.hpp"
#include "Message.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"

using namespace std;

string Folder::TABLE_NAME = "Folder";

Folder::Folder(json & json) : MailModel(json) {
    
}

Folder::Folder(string id, string accountId, int version) :
    MailModel(id, accountId, version)
{
    _data["path"] = "";
    _data["role"] = "";
}

Folder::Folder(SQLite::Statement & query) :
    MailModel(query)
{
}

json & Folder::localStatus() {
    return _data["localStatus"];
}

string Folder::path() {
    return _data["path"].get<string>();
}

void Folder::setPath(string path) {
    _data["path"] = path;
}

string Folder::role() const {
    return _data["role"].get<string>();
}

void Folder::setRole(string role) {
    _data["role"] = role;
}

string Folder::tableName() {
    return Folder::TABLE_NAME;
}

vector<string> Folder::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "path", "role"};
}

void Folder::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":path", path());
    query->bind(":role", role());
}

void Folder::beforeSave(MailStore * store) {
    MailModel::beforeSave(store);

    if (version() == 1) {
        // ensure ThreadCounts entry is present
        SQLite::Statement count(store->db(), "INSERT OR IGNORE INTO ThreadCounts (categoryId, unread, total) VALUES (?, 0, 0)");
        count.bind(1, id());
        count.exec();
    }
}

/*
 Removing a folder (or a Label, which inherits this) deletes every placement in it as one
 statement. The messages that held them are not loaded here - this runs inside the caller's
 transaction and a folder can hold every message of the account - so their ids are kept
 for the caller to rewrite in its own short transactions afterwards
 (MailProcessor::saveMessagesAfterPlacementChange).
 */
void Folder::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);

    SQLite::Statement count(store->db(), "DELETE FROM ThreadCounts WHERE categoryId = ?");
    count.bind(1, id());
    count.exec();

    _messageIdsAffectedByRemove = store->deletePlacementsForFolder(id());
}

const vector<string> & Folder::messageIdsAffectedByRemove() {
    return _messageIdsAffectedByRemove;
}
