#include "mailsync/models/calendar.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/mail_store.hpp"

using namespace std;

string Calendar::TABLE_NAME = "Calendar";

Calendar::Calendar(json & json) : MailModel(json) {

}

Calendar::Calendar(string id, string accountId) :
    MailModel(id, accountId, 0)
{
}

Calendar::Calendar(SQLite::Statement & query) :
    MailModel(query)
{
}


string Calendar::path() {
    return _data["path"].get<string>();
}

void Calendar::setPath(string path) {
    _data["path"] = path;
}

string Calendar::name() {
    return _data["name"].get<string>();
}

void Calendar::setName(string name) {
    _data["name"] = name;
}

string Calendar::tableName() {
    return Calendar::TABLE_NAME;
}

vector<string> Calendar::columnsForQuery() {
    return vector<string>{"id", "data", "accountId"};
}

void Calendar::bindToQuery(SQLite::Statement * query) {
    query->bind(":id", id());
    query->bind(":data", toJSON().dump());
    query->bind(":accountId", accountId());
}
