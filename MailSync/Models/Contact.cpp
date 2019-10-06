//
//  Contact.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Contact.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string Contact::TABLE_NAME = "Contact";

Contact::Contact(string id, string accountId, string email, int refs, string source) : MailModel(id, accountId) {
    _data["email"] = email;
    _data["s"] = source;
    _data["refs"] = refs;
}

Contact::Contact(json json) :
    MailModel(json)
{
}

Contact::Contact(SQLite::Statement & query) :
    MailModel(query)
{
}

string Contact::constructorName() {
    return _data["__cls"].get<string>();
}

string Contact::tableName() {
    return Contact::TABLE_NAME;
}

string Contact::name() {
    return _data["name"].get<string>();
}

void Contact::setName(string name) {
    _data["name"] = name;
}

string Contact::email() {
    return _data["email"].get<string>();
}

void Contact::setEmail(string email) {
    _data["email"] = email;
}

string Contact::source() {
    return _data.count("s") ? _data["s"].get<string>() : "";
}

string Contact::etag() {
    return _data.count("etag") ? _data["etag"].get<string>() : "";
}

unordered_set<string> Contact::groupIds() {
    unordered_set<string> set {};
    if (!_data.count("gis")) return set;
    for (auto item : _data["gis"]) {
        set.insert(item.get<string>());
    }
    return set;
}

void Contact::setGroupIds(unordered_set<string> set) {
    vector<string> arr;
    for (auto item : set) {
        arr.push_back(item);
    }
    _data["gis"] = arr;
}

void Contact::setEtag(string etag) {
    _data["etag"] = etag;
}

string Contact::remoteCollectionId() {
    return _data.count("rci") ? _data["rci"].get<string>() : "";
}

void Contact::setRemoteCollectionId(string rci) {
    _data["rci"] = rci;
}

json Contact::info() {
    return _data["info"];
}

void Contact::setInfo(json info) {
    _data["info"] = info;
}

bool Contact::hidden() {
    return _data.count("h") ? _data["h"].get<bool>() : false;
}
void Contact::setHidden(bool b) {
    _data["h"] = b;
}

string Contact::searchContent() {
    string content = _data["email"].get<string>();
    
    size_t at = content.find("@");
    if (at != string::npos) {
        content = content.replace(at, 1, " ");
    }
    if (_data.count("name")) {
        content = content + " " + _data["name"].get<string>();
    }

    return content;
}

int Contact::refs() {
    return _data["refs"].get<int>();
}

void Contact::incrementRefs() {
    _data["refs"] = refs() + 1;
}

vector<string> Contact::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "refs", "email", "hidden", "source", "etag", "remoteCollectionId" };
}

void Contact::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":refs", refs());
    query->bind(":email", email());
    query->bind(":hidden", hidden() ? 1 : 0);
    query->bind(":source", source());
    query->bind(":etag", etag());
    query->bind(":remoteCollectionId", remoteCollectionId());
}


void Contact::afterSave(MailStore * store) {
    MailModel::afterSave(store);

    if (version() == 1) {
        SQLite::Statement insert(store->db(), "INSERT INTO ContactSearch (content_id, content) VALUES (?, ?)");
        insert.bind(1, id());
        insert.bind(2, searchContent());
        insert.exec();
    } else if (source() != CONTACT_SOURCE_MAIL) {
        SQLite::Statement update(store->db(), "UPDATE ContactSearch SET content = ? WHERE content_id = ?");
        update.bind(1, searchContent());
        update.bind(2, id());
        update.exec();
    }
}

void Contact::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);
    
    SQLite::Statement update(store->db(), "DELETE FROM ContactSearch WHERE content_id = ?");
    update.bind(1, id());
    update.exec();
}
