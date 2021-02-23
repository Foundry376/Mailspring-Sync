#include "mailsync/models/calendar.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/mail_store.hpp"



std::string Calendar::TABLE_NAME = "Calendar";

Calendar::Calendar(nlohmann::json & json) : MailModel(json) {

}

Calendar::Calendar(std::string id, std::string accountId) :
    MailModel(id, accountId, 0)
{
}

Calendar::Calendar(SQLite::Statement & query) :
    MailModel(query)
{
}


std::string Calendar::path() {
    return _data["path"].get<std::string>();
}

void Calendar::setPath(std::string path) {
    _data["path"] = path;
}

std::string Calendar::name() {
    return _data["name"].get<std::string>();
}

void Calendar::setName(std::string name) {
    _data["name"] = name;
}

std::string Calendar::tableName() {
    return Calendar::TABLE_NAME;
}

std::vector<std::string> Calendar::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId"};
}

void Calendar::bindToQuery(SQLite::Statement * query) {
    query->bind(":id", id());
    query->bind(":data", toJSON().dump());
    query->bind(":accountId", accountId());
}
