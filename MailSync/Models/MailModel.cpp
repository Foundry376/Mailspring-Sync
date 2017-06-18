//
//  MailModel.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailModel.hpp"

MailModel::MailModel(std::string id, int version) :
    _id(id),
    _version(version)
{
    
}

MailModel::MailModel(SQLite::Statement & query) :
    _id(query.getColumn("id").getString()),
    _version(query.getColumn("version").getInt())
{
}

std::string MailModel::id() {
    return _id;
}

int MailModel::getVersion() {
    return _version;
}

void MailModel::incrementVersion() {
    _version ++;
}

