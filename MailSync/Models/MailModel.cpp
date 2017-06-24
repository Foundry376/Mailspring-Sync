//
//  MailModel.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailModel.hpp"

using namespace std;

string MailModel::TABLE_NAME = "MailModel";

MailModel::MailModel(string id, string accountId, int version) :
    _data({{"id", id},{"accountId", accountId}, {"version", version}})
{
}

MailModel::MailModel(SQLite::Statement & query) :
    _data(json::parse(query.getColumn("data").getString()))
{
}


MailModel::MailModel(json json) :
    _data(json)
{
}

string MailModel::id()
{
    return _data["id"].get<std::string>();
}

string MailModel::accountId()
{
    return _data["accountId"].get<std::string>();
}

int MailModel::version()
{
    return _data["version"].get<int>();
}

void MailModel::incrementVersion()
{
    _data["version"] = _data["version"].get<int>() + 1;
}

string MailModel::tableName()
{
    return TABLE_NAME;
}

json MailModel::toJSON()
{
    // note: do not override for Task!
    if (!_data.count("__constructorName")) {
        _data["__constructorName"] = this->tableName();
    }
    return _data;
}

json MailModel::toJSONDispatch()
{
    return this->toJSON();
}

void MailModel::bindToQuery(SQLite::Statement & query) {
    query.bind(":id", id());
    query.bind(":data", this->toJSON().dump());
    query.bind(":accountId", accountId());
    query.bind(":version", version());
}

void MailModel::writeAssociations(SQLite::Database & db) {
    
}


