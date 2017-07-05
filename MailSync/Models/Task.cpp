//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Task.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string Task::TABLE_NAME = "Task";

Task::Task(json json) : MailModel(json) {
    
}

Task::Task(SQLite::Statement & query) :
    MailModel(query)
{
}

string Task::status() {
    return _data["status"].get<string>();
}

void Task::setStatus(string s) {
    _data["status"] = s;
}

json Task::error() {
    return _data["error"];
}

void Task::setError(json e) {
    _data["error"] = e;
}

json & Task::data() {
    return _data;
}

string Task::constructorName() {
    return _data["__cls"].get<string>();
}

string Task::tableName() {
    return Task::TABLE_NAME;
}

vector<string> Task::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "status"};
}

void Task::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":status", status());
}
