//
//  Contact.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Contact.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string Contact::TABLE_NAME = "Contact";

Contact::Contact(string accountId, string canonicalizedEmail, json json) : MailModel(json) {
    _data["aid"] = accountId;
    _data["email"] = canonicalizedEmail;
    if (!_data.count("refs")) {
        _data["refs"] = 0;
    }
    if (!_data.count("v")) {
        _data["v"] = 0;
    }
    if (!_data.count("id")) {
        _data["id"] = _data["email"];
    }
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

string Contact::email() {
    return _data["email"].get<string>();
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
    return vector<string>{"id", "data", "accountId", "version", "refs", "email"};
}

void Contact::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":refs", refs());
    query->bind(":email", email());
}
