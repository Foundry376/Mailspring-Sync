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

#include "ContactGroup.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string ContactGroup::TABLE_NAME = "ContactGroup";

ContactGroup::ContactGroup(string id, string accountId) :
    MailModel(id, accountId)
{
}

ContactGroup::ContactGroup(SQLite::Statement & query) :
    MailModel(query)
{
}

string ContactGroup::constructorName() {
    return _data["__cls"].get<string>();
}

string ContactGroup::tableName() {
    return ContactGroup::TABLE_NAME;
}

string ContactGroup::name() {
    return _data["name"].get<string>();
}

void ContactGroup::setName(string name) {
    _data["name"] = name;
}

vector<string> ContactGroup::columnsForQuery() {
    return vector<string>{"id", "accountId", "version", "data", "name"};
}

void ContactGroup::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":name", name());
}


void ContactGroup::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);
    
    // ensure the join table has been emptied of our items
    SQLite::Statement update(store->db(), "DELETE FROM ContactContactGroup WHERE value = ?");
    update.bind(1, id());
    update.exec();
}
