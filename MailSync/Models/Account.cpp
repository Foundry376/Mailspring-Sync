//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Account.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string Account::TABLE_NAME = "Account";

Account::Account(json json) : MailModel(json) {
    
}

Account::Account(SQLite::Statement & query) :
    MailModel(query)
{
}

bool Account::valid() {
    if (!_data.count("id") || !_data.count("settings")) {
        return false;
    }
    json & settings = _data["settings"];
    return settings.count("imap_port") && settings.count("imap_host") && settings.count("imap_username") && settings.count("imap_password");
}

unsigned int Account::IMAPPort() {
    return _data["settings"]["imap_port"].get<unsigned int>();
}

string Account::IMAPHost() {
    return _data["settings"]["imap_host"].get<string>();
}

string Account::IMAPUsername() {
    return _data["settings"]["imap_username"].get<string>();
}

string Account::IMAPPassword() {
    return _data["settings"]["imap_password"].get<string>();
}

string Account::constructorName() {
    return _data["__cls"].get<string>();
}

string Account::tableName() {
    return Account::TABLE_NAME;
}

vector<string> Account::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "status"};
}

void Account::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
}
