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

string Account::valid() {
    if (!_data.count("id") || !_data.count("settings")) {
        return "id or settings";
    }
    if (!_data.count("provider")) {
        return "provider";
    }

    json & s = _data["settings"];
    if (!(s.count("imap_port") && s.count("imap_host") && s.count("imap_username") && (s.count("refresh_token") || s.count("imap_password")))) {
        return "imap_*";
    }
    if (!(s.count("smtp_port") && s.count("smtp_host") && s.count("smtp_username") && (s.count("refresh_token") || s.count("smtp_password")))) {
        return "smtp_*";
    }
    if (!(s.count("imap_allow_insecure_ssl") && s["imap_allow_insecure_ssl"].is_boolean())) {
        return "imap_allow_insecure_ssl";
    }
    if (!(s.count("smtp_allow_insecure_ssl") && s["smtp_allow_insecure_ssl"].is_boolean())) {
        return "smtp_allow_insecure_ssl";
    }
    return ""; // true
}

string Account::provider() {
    return _data["provider"].get<string>();
}

string Account::refreshToken() {
    if (_data["settings"].count("refresh_token") == 0) {
        return "";
    }
    return _data["settings"]["refresh_token"].get<string>();
}

unsigned int Account::IMAPPort() {
    json & val = _data["settings"]["imap_port"];
    return val.is_string() ? stoi(val.get<string>()) : val.get<unsigned int>();
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

string Account::IMAPSecurity() {
    return _data["settings"]["imap_security"].get<string>();
}

bool Account::IMAPAllowInsecureSSL() {
    return _data["settings"]["imap_allow_insecure_ssl"].get<bool>();
}

unsigned int Account::SMTPPort() {
    json & val = _data["settings"]["smtp_port"];
    return val.is_string() ? stoi(val.get<string>()) : val.get<unsigned int>();
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

string Account::SMTPSecurity() {
    return _data["settings"]["smtp_security"].get<string>();
}

bool Account::SMTPAllowInsecureSSL() {
    return _data["settings"]["smtp_allow_insecure_ssl"].get<bool>();
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
