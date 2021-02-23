#include "mailsync/models/contact.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"

#include <functional>




std::string Contact::TABLE_NAME = "Contact";

Contact::Contact(std::string id, std::string accountId, std::string email, int refs, std::string source) : MailModel(id, accountId) {
    _data["email"] = email;
    _data["s"] = source;
    _data["refs"] = refs;
    _data["gis"] = nlohmann::json::array();
    _data["info"] = nlohmann::json::object();
    _data["name"] = "";
}

Contact::Contact(nlohmann::json json) :
    MailModel(json)
{
}

Contact::Contact(SQLite::Statement & query) :
    MailModel(query)
{
}

std::string Contact::constructorName() {
    return _data["__cls"].get<std::string>();
}

std::string Contact::tableName() {
    return Contact::TABLE_NAME;
}

std::string Contact::name() {
    return _data["name"].get<std::string>();
}

void Contact::setName(std::string name) {
    _data["name"] = name;
}

std::string Contact::googleResourceName() {
    return _data.count("grn") ? _data["grn"].get<std::string>() : "";
}

void Contact::setGoogleResourceName(std::string rn) {
    _data["grn"] = rn;
}

std::string Contact::email() {
    return _data["email"].get<std::string>();
}

void Contact::setEmail(std::string email) {
    _data["email"] = email;
}

std::string Contact::source() {
    return _data.count("s") ? _data["s"].get<std::string>() : CONTACT_SOURCE_MAIL;
}

std::string Contact::etag() {
    return _data.count("etag") ? _data["etag"].get<std::string>() : "";
}

std::unordered_set<std::string> Contact::groupIds() {
    std::unordered_set<std::string> set {};
    if (!_data.count("gis")) return set;
    for (auto item : _data["gis"]) {
        set.insert(item.get<std::string>());
    }
    return set;
}

void Contact::setGroupIds(std::unordered_set<std::string> set) {
    std::vector<std::string> arr;
    for (auto item : set) {
        arr.push_back(item);
    }
    _data["gis"] = arr;
}

void Contact::setEtag(std::string etag) {
    _data["etag"] = etag;
}

std::string Contact::bookId() {
    return _data.count("bid") ? _data["bid"].get<std::string>() : "";
}

void Contact::setBookId(std::string rci) {
    _data["bid"] = rci;
}

nlohmann::json Contact::info() {
    return _data["info"];
}

void Contact::setInfo(nlohmann::json info) {
    _data["info"] = info;
}

bool Contact::hidden() {
    return _data.count("h") ? _data["h"].get<bool>() : false;
}
void Contact::setHidden(bool b) {
    _data["h"] = b;
}

std::string Contact::searchContent() {
    std::string content = _data["email"].get<std::string>();

    size_t at = content.find("@");
    if (at != std::string::npos) {
        content = content.replace(at, 1, " ");
    }
    if (_data.count("name")) {
        content = content + " " + _data["name"].get<std::string>();
    }

    return content;
}

int Contact::refs() {
    return _data["refs"].get<int>();
}

void Contact::incrementRefs() {
    _data["refs"] = refs() + 1;
}

void Contact::mutateCardInInfo(std::function<void(std::shared_ptr<VCard>)> yieldBlock) {
    auto nextInfo = info();
    auto card = std::make_shared<VCard>(nextInfo["vcf"].get<std::string>());
    if (card->incomplete()) {
        return;
    }

    yieldBlock(card);
    nextInfo["vcf"] = card->serialize();
    setInfo(nextInfo);
}

std::vector<std::string> Contact::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId", "version", "refs", "email", "hidden", "source", "etag", "bookId" };
}

void Contact::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":refs", refs());
    query->bind(":email", email());
    query->bind(":hidden", hidden() ? 1 : 0);
    query->bind(":source", source());
    query->bind(":etag", etag());
    query->bind(":bookId", bookId());
}


void Contact::afterSave(MailStore * store) {
    MailModel::afterSave(store);

    if (version() == 1) {
        SQLite::Statement insert(store->db(), "INSERT INTO ContactSearch (content_id, content) VALUES (?, ?)");
        insert.bind(1, id());
        insert.bind(2, searchContent());
        insert.exec();
    } else if (source() != CONTACT_SOURCE_MAIL) {
        SQLite::Statement update(store->db(), "UPDATE ContactSearch SET content = ? WHERE content_id = ?");
        update.bind(1, searchContent());
        update.bind(2, id());
        update.exec();
    }
}

void Contact::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);

    SQLite::Statement update(store->db(), "DELETE FROM ContactSearch WHERE content_id = ?");
    update.bind(1, id());
    update.exec();
}
