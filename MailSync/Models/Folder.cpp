//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Folder.hpp"

Folder::Folder(std::string id, int version) :
MailModel(id, version)
{
}

Folder::Folder(SQLite::Statement & query) :
MailModel(query),
_localStatus(json::parse(query.getColumn("localStatus").getString())),
_path(query.getColumn("path").getString())
{
    
}


json & Folder::localStatus() {
    return _localStatus;
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
    return "folders";
}

std::vector<std::string> Folder::columnsForQuery() {
    return std::vector<std::string>{"id", "version", "path", "localStatus", "role"};
}

void Folder::bindToQuery(SQLite::Statement & query) {
    query.bind(":id", _id);
    query.bind(":version", _version);
    query.bind(":path", _path);
    query.bind(":role", _role);
    query.bind(":localStatus", _localStatus.dump());
}


