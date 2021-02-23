#include "mailsync/models/task.hpp"
#include "mailsync/mail_utils.hpp"
#include "mailsync/models/thread.hpp"
#include "mailsync/models/message.hpp"




std::string Task::TABLE_NAME = "Task";

Task::Task(std::string constructorName, std::string accountId, nlohmann::json taskSpecificData) :
    MailModel(MailUtils::idRandomlyGenerated(), accountId) {
    _data["__cls"] = constructorName;
    _data["status"] = "local";
    for (nlohmann::json::iterator it = taskSpecificData.begin(); it != taskSpecificData.end(); ++it) {
        _data[it.key()] = it.value();
    }
}

Task::Task(nlohmann::json json) : MailModel(json) {

}

Task::Task(SQLite::Statement & query) :
    MailModel(query)
{
}

std::string Task::status() {
    return _data["status"].get<std::string>();
}

void Task::setStatus(std::string s) {
    _data["status"] = s;
}

bool Task::shouldCancel() {
    return _data.count("should_cancel") && _data["should_cancel"].get<bool>();
}

void Task::setShouldCancel() {
    _data["should_cancel"] = true;
}


nlohmann::json Task::error() {
    return _data["error"];
}

void Task::setError(nlohmann::json e) {
    _data["error"] = e;
}

nlohmann::json & Task::data() {
    return _data;
}

std::string Task::constructorName() {
    return _data["__cls"].get<std::string>();
}

std::string Task::tableName() {
    return Task::TABLE_NAME;
}

std::vector<std::string> Task::columnsForQuery() {
    return std::vector<std::string>{"id", "data", "accountId", "version", "status"};
}

void Task::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":status", status());
}
