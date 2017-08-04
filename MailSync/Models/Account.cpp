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
    if (!_data.count("id") || !_data.count("settings") || !_data.count("cloudToken")) {
        return false;
    }
    json & settings = _data["settings"];
    if (!(settings.count("imap_port") && settings.count("imap_host") && settings.count("imap_username") && settings.count("imap_password"))) {
        return false;
    }
    if (!(settings.count("smtp_port") && settings.count("smtp_host") && settings.count("smtp_username") && settings.count("smtp_password"))) {
        return false;
    }
    return true;
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

unsigned int Account::SMTPPort() {
    return _data["settings"]["smtp_port"].get<unsigned int>();
}

string Account::SMTPHost() {
    return _data["settings"]["smtp_host"].get<string>();
}

string Account::SMTPUsername() {
    return _data["settings"]["smtp_username"].get<string>();
}

string Account::SMTPPassword() {
    return _data["settings"]["smtp_password"].get<string>();
}

string Account::cloudToken() {
    return _data["cloudToken"].get<string>();
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
