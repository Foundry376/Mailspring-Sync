//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Folder.hpp"
#include "MailUtils.hpp"

Folder::Folder(std::string id, std::string accountId, int version) :
MailModel(id, accountId, version)
{
}

Folder::Folder(SQLite::Statement & query) :
MailModel(query),
_localStatus(json::parse(query.getColumn("localStatus").getString())),
_path(query.getColumn("path").getString())
{
    
}


json & Folder::localStatus() {
    return this->_localStatus;
}

std::string Folder::path() {
    return _path;
}

void Folder::setPath(std::string path) {
    _path = path;
}

std::string Folder::role() {
    return _role;
}

void Folder::setRole(std::string role) {
    _role = role;
}

std::string Folder::tableName() {
    return "Folder";
}

std::vector<std::string> Folder::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId", "version", "path", "localStatus", "role"};
}

void Folder::bindToQuery(SQLite::Statement & query) {
    MailModel::bindToQuery(query);
    query.bind(":path", _path);
    query.bind(":role", _role);
    query.bind(":localStatus", _localStatus.dump());
}

json Folder::toJSON()
{
    return MailUtils::merge(MailModel::toJSON(), {
        {"object", "folder"},
        {"path", _path},
        {"role", _role},
        {"localStatus", _localStatus}
    });
}


