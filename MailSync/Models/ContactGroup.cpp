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

string ContactGroup::bookId() {
    return _data.count("bid") ? _data["bid"].get<string>() : "";
}

void ContactGroup::setBookId(string rci) {
    _data["bid"] = rci;
}

string ContactGroup::googleResourceName() {
    return _data.count("grn") ? _data["grn"].get<string>() : "";
}

void ContactGroup::setGoogleResourceName(string rn) {
    _data["grn"] = rn;
}

vector<string> ContactGroup::columnsForQuery() {
    return vector<string>{"id", "accountId", "version", "data", "name", "bookId"};
}

void ContactGroup::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":name", name());
    query->bind(":bookId", bookId());
}


void ContactGroup::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);
    
    // ensure the join table has been emptied of our items
    SQLite::Statement update(store->db(), "DELETE FROM ContactContactGroup WHERE value = ?");
    update.bind(1, id());
    update.exec();
}

vector<string> ContactGroup::getMembers(MailStore * store) {
    SQLite::Statement find(store->db(), "SELECT id FROM ContactContactGroup WHERE value = ?");
    find.bind(1, id());
    vector<string> contactIds;
    while (find.executeStep()) {
        contactIds.push_back(find.getColumn("id").getString());
    }
    return contactIds;
}

void ContactGroup::syncMembers(MailStore * store, vector<string> newContactIds) {
    store->beginTransaction();
    
    vector<string> oldContactIds = getMembers(store);
    
    // remove all the join table entries
    SQLite::Statement removeMembers(store->db(), "DELETE FROM ContactContactGroup WHERE value = ?");
    removeMembers.bind(1, id());
    removeMembers.exec();

    // create new join table entries
    SQLite::Statement insertMembers(store->db(), "INSERT OR IGNORE INTO ContactContactGroup (id, value) VALUES (?,?)");
    for (auto contactId : newContactIds) {
        insertMembers.bind(1, contactId);
        insertMembers.bind(2, id());
        insertMembers.exec();
        insertMembers.reset();
    }
    
    // update the actual contacts. This is unfortunately necessary because Mailspring
    // will not update live queries unless the membershsip data is on the actual models
    vector<string> all {};
    all.insert(all.end(), oldContactIds.begin(), oldContactIds.end());
    all.insert(all.end(), newContactIds.begin(), newContactIds.end());


    auto contacts = store->findLargeSet<Contact>("id", all);
    for (auto contact : contacts) {
        auto groupIds = contact->groupIds();
        bool inGroup = std::find(newContactIds.begin(), newContactIds.end(), contact->id()) != newContactIds.end();
        if (inGroup && groupIds.count(id()) == 0) {
            // they are in new contacts
            groupIds.insert(id());
            contact->setGroupIds(groupIds);
            store->save(contact.get());
            
        } else if (!inGroup && groupIds.count(id()) > 0) {
            // they are not in new contacts
            groupIds.erase(id());
            contact->setGroupIds(groupIds);
            store->save(contact.get());
        }
    }
    
    store->commitTransaction();
}
