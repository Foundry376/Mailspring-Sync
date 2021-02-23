#include "mailsync/models/folder.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/mail_store.hpp"



std::string Folder::TABLE_NAME = "Folder";

Folder::Folder(nlohmann::json & json) : MailModel(json) {

}

Folder::Folder(std::string id, std::string accountId, int version) :
    MailModel(id, accountId, version)
{
    _data["path"] = "";
    _data["role"] = "";
}

Folder::Folder(SQLite::Statement & query) :
    MailModel(query)
{
}

nlohmann::json & Folder::localStatus() {
    return _data["localStatus"];
}

std::string Folder::path() {
    return _data["path"].get<std::string>();
}

void Folder::setPath(std::string path) {
    _data["path"] = path;
}

std::string Folder::role() const {
    return _data["role"].get<std::string>();
}

void Folder::setRole(std::string role) {
    _data["role"] = role;
}

std::string Folder::tableName() {
    return Folder::TABLE_NAME;
}

std::vector<std::string> Folder::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId", "version", "path", "role"};
}

void Folder::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":path", path());
    query->bind(":role", role());
}

void Folder::beforeSave(MailStore * store) {
    MailModel::beforeSave(store);

    if (version() == 1) {
        // ensure ThreadCounts entry is present
        SQLite::Statement count(store->db(), "INSERT OR IGNORE INTO ThreadCounts (categoryId, unread, total) VALUES (?, 0, 0)");
        count.bind(1, id());
        count.exec();
    }
}

void Folder::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);

    SQLite::Statement count(store->db(), "DELETE FROM ThreadCounts WHERE categoryId = ?");
    count.bind(1, id());
    count.exec();
}
