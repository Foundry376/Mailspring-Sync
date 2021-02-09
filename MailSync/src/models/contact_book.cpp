#include "mailsync/models/contact_book.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"

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

vector<string> ContactBook::columnsForQuery() {
    return vector<string>{"id", "accountId", "version", "data"};
}

void ContactBook::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
}
