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
    _id(id),
    _data("{}"),
    _accountId(accountId),
    _version(version)
{
    
}

MailModel::MailModel(SQLite::Statement & query) :
    _id(query.getColumn("id").getString()),
    _data(query.getColumn("data").getString()),
    _accountId(query.getColumn("accountId").getString()),
    _version(query.getColumn("version").getInt())
{
}

string MailModel::id()
{
    return _id;
}

string MailModel::accountId()
{
    return _accountId;
}

int MailModel::version()
{
    return _version;
}

void MailModel::incrementVersion()
{
    _version ++;
}

string MailModel::tableName()
{
    return TABLE_NAME;
}

json MailModel::toJSON()
{
    return {
        {"id", _id},
        {"accountId", _accountId},
        {"version", _version},
    };
}


void MailModel::bindToQuery(SQLite::Statement & query) {
    query.bind(":id", _id);
    query.bind(":data", this->toJSON().dump());
    query.bind(":accountId", _accountId);
    query.bind(":version", _version);
}


