#include "mailsync/models/contact_book.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"




std::string ContactBook::TABLE_NAME = "ContactBook";

ContactBook::ContactBook(std::string id, std::string accountId) :
    MailModel(id, accountId)
{
    setSource("");
    setURL("");
}

ContactBook::ContactBook(SQLite::Statement & query) :
    MailModel(query)
{
}

std::string ContactBook::constructorName() {
    return _data["__cls"].get<std::string>();
}

std::string ContactBook::tableName() {
    return ContactBook::TABLE_NAME;
}

std::string ContactBook::url() {
    return _data["url"].get<std::string>();
}

void ContactBook::setURL(std::string url) {
    _data["url"] = url;
}

std::string ContactBook::source() {
    return _data["source"].get<std::string>();
}

void ContactBook::setSource(std::string source) {
    _data["source"] = source;
}

std::vector<std::string> ContactBook::columnsForQuery() {
    return std::vector<std::string>{"id", "accountId", "version", "data"};
}

void ContactBook::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
}
