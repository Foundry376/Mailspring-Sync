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
    return true;
}

unsigned int Account::IMAPPort() {
    return _data["imap_port"].get<unsigned int>();
}

string Account::IMAPHost() {
    return _data["imap_host"].get<string>();
}

string Account::IMAPUsername() {
    return _data["imap_username"].get<string>();
}

string Account::IMAPPassword() {
    return _data["imap_password"].get<string>();
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
