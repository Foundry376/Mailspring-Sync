//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
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

int Account::startDelay() {
    string s = id().substr(0, 1);
    if (s == "0" || s == "1" || s == "2") return 1;
    if (s == "3" || s == "4" || s == "5") return 2;
    if (s == "6" || s == "7" || s == "8") return 3;
    if (s == "9" || s == "a" || s == "b") return 4;
    return 0;
}

string Account::valid() {
    if (!_data.count("id") || !_data.count("settings")) {
        return "id or settings";
    }
    if (!_data.count("provider")) {
        return "provider";
    }

    json & s = _data["settings"];

    if (!(s.count("refresh_token") || s.count("imap_password"))) {
        return "imap_password or refresh_token";
    }
    if (!(s.count("refresh_token") || s.count("smtp_password"))) {
        return "smtp_password or refresh_token";
    }
    if (!(s.count("imap_port") && s.count("imap_host") && s.count("imap_username"))) {
        return "imap configuration";
    }
    if (!(s.count("smtp_port") && s.count("smtp_host"))) {
        return "smtp configuration";
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

string Account::emailAddress() {
    return _data["emailAddress"].get<string>();
}

string Account::refreshToken() {
    if (_data["settings"].count("refresh_token") == 0) {
        return "";
    }
    return _data["settings"]["refresh_token"].get<string>();
}

void Account::setRefreshToken(string value) {
    _data["settings"]["refresh_token"] = value;
}

string Account::refreshClientId() {
    if (_data["settings"].count("refresh_client_id") == 0) {
        return "";
    }
    return _data["settings"]["refresh_client_id"].get<string>();
}

unsigned int Account::IMAPPort() {
    json & val = _data["settings"]["imap_port"];
    return val.is_string() ? stoi(val.get<string>()) : val.get<unsigned int>();
}

string Account::IMAPHost() {
    return _data["settings"]["imap_host"].get<string>();
}

string Account::IMAPUsername() {
    json & s = _data["settings"];
    return s.count("imap_username") ? s["imap_username"].get<string>() : "";
}

string Account::IMAPPassword() {
    json & s = _data["settings"];
    return s.count("imap_password") ? s["imap_password"].get<string>() : "";
}

string Account::IMAPSecurity() {
    return _data["settings"]["imap_security"].get<string>();
}

bool Account::IMAPAllowInsecureSSL() {
    return _data["settings"]["imap_allow_insecure_ssl"].get<bool>();
}

bool Account::isICloud() {
    return IMAPHost().find("imap.mail.me.com") != string::npos;
}

unsigned int Account::SMTPPort() {
    json & val = _data["settings"]["smtp_port"];
    return val.is_string() ? stoi(val.get<string>()) : val.get<unsigned int>();
}

string Account::SMTPHost() {
    return _data["settings"]["smtp_host"].get<string>();
}

string Account::SMTPUsername() {
    json & s = _data["settings"];
    return s.count("smtp_username") ? s["smtp_username"].get<string>() : "";
}

string Account::SMTPPassword() {
    json & s = _data["settings"];
    return s.count("smtp_password") ? s["smtp_password"].get<string>() : "";
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

/* Account objects are not stored in the database. */

string Account::tableName(){
    assert(false);
    return "";
}

vector<string> Account::columnsForQuery() {
    assert(false);
    return {};
}

void Account::bindToQuery(SQLite::Statement * query) {
    assert(false);
}

string Account::containerFolder() {
    json & s = _data["settings"];
    return s.count("container_folder") ? s["container_folder"].get<string>() : "";
}
