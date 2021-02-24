#include "mailsync/models/account.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"




std::string Account::TABLE_NAME = "Account";

Account::Account(nlohmann::json json) : MailModel(json) {

}

Account::Account(SQLite::Statement & query) :
    MailModel(query)
{
}

int Account::startDelay() {
    std::string s = id().substr(0, 1);
    if (s == "0" || s == "1" || s == "2") return 1;
    if (s == "3" || s == "4" || s == "5") return 2;
    if (s == "6" || s == "7" || s == "8") return 3;
    if (s == "9" || s == "a" || s == "b") return 4;
    return 0;
}

std::string Account::valid() {
    if (!_data.count("id") || !_data.count("settings")) {
        return "id or settings";
    }
    if (!_data.count("provider")) {
        return "provider";
    }

    nlohmann::json & s = _data["settings"];

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

std::string Account::provider() {
    return _data["provider"].get<std::string>();
}

std::string Account::emailAddress() {
    return _data["emailAddress"].get<std::string>();
}

std::string Account::refreshToken() {
    if (_data["settings"].count("refresh_token") == 0) {
        return "";
    }
    return _data["settings"]["refresh_token"].get<std::string>();
}

std::string Account::refreshClientId() {
    if (_data["settings"].count("refresh_client_id") == 0) {
        return "";
    }
    return _data["settings"]["refresh_client_id"].get<std::string>();
}

unsigned int Account::IMAPPort() {
    nlohmann::json & val = _data["settings"]["imap_port"];
    return val.is_string() ? std::stoi(val.get<std::string>()) : val.get<unsigned int>();
}

std::string Account::IMAPHost() {
    return _data["settings"]["imap_host"].get<std::string>();
}

std::string Account::IMAPUsername() {
    nlohmann::json & s = _data["settings"];
    return s.count("imap_username") ? s["imap_username"].get<std::string>() : "";
}

std::string Account::IMAPPassword() {
    nlohmann::json & s = _data["settings"];
    return s.count("imap_password") ? s["imap_password"].get<std::string>() : "";
}

std::string Account::IMAPSecurity() {
    return _data["settings"]["imap_security"].get<std::string>();
}

bool Account::IMAPAllowInsecureSSL() {
    return _data["settings"]["imap_allow_insecure_ssl"].get<bool>();
}

unsigned int Account::SMTPPort() {
    nlohmann::json & val = _data["settings"]["smtp_port"];
    return val.is_string() ? std::stoi(val.get<std::string>()) : val.get<unsigned int>();
}

std::string Account::SMTPHost() {
    return _data["settings"]["smtp_host"].get<std::string>();
}

std::string Account::SMTPUsername() {
    nlohmann::json & s = _data["settings"];
    return s.count("smtp_username") ? s["smtp_username"].get<std::string>() : "";
}

std::string Account::SMTPPassword() {
    nlohmann::json & s = _data["settings"];
    return s.count("smtp_password") ? s["smtp_password"].get<std::string>() : "";
}

std::string Account::SMTPSecurity() {
    return _data["settings"]["smtp_security"].get<std::string>();
}

bool Account::SMTPAllowInsecureSSL() {
    return _data["settings"]["smtp_allow_insecure_ssl"].get<bool>();
}

std::string Account::constructorName() {
    return _data["__cls"].get<std::string>();
}

/* Account objects are not stored in the database. */

std::string Account::tableName(){
    assert(false);
    return "";
}

std::vector<std::string> Account::columnsForQuery() {
    assert(false);
    return {};
}

void Account::bindToQuery(SQLite::Statement * query) {
    assert(false);
}
