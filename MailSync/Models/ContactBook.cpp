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

#include "ContactBook.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string ContactBook::TABLE_NAME = "ContactBook";

ContactBook::ContactBook(string id, string accountId) :
    MailModel(id, accountId)
{
    setSource("");
    setURL("");
}

ContactBook::ContactBook(SQLite::Statement & query) :
    MailModel(query)
{
}

string ContactBook::constructorName() {
    return _data["__cls"].get<string>();
}

string ContactBook::tableName() {
    return ContactBook::TABLE_NAME;
}

string ContactBook::url() {
    return _data["url"].get<string>();
}

void ContactBook::setURL(string url) {
    _data["url"] = url;
}

string ContactBook::source() {
    return _data["source"].get<string>();
}

void ContactBook::setSource(string source) {
    _data["source"] = source;
}

string ContactBook::ctag() {
    return _data.count("ctag") ? _data["ctag"].get<string>() : "";
}

void ContactBook::setCtag(string ctag) {
    _data["ctag"] = ctag;
}

string ContactBook::syncToken() {
    return _data.count("syncToken") ? _data["syncToken"].get<string>() : "";
}

void ContactBook::setSyncToken(string token) {
    _data["syncToken"] = token;
}

vector<string> ContactBook::columnsForQuery() {
    return vector<string>{"id", "accountId", "version", "data"};
}

void ContactBook::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
}

