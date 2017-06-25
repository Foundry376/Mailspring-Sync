//
//  Message.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Message.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"
#include "MailStore.hpp"
#include "File.hpp"

using namespace std;

string Message::TABLE_NAME = "Message";

Message::Message(mailcore::IMAPMessage * msg, Folder & folder) :
MailModel(MailUtils::idForMessage(msg), folder.accountId(), 0)
{
    _data["folder"] = folder.toJSON();
    _data["date"] = msg->header()->date();
    _data["headerMessageId"] = msg->header()->messageID()->UTF8Characters();
    _data["subject"] = msg->header()->subject() ? msg->header()->subject()->UTF8Characters() : "No Subject";
    _data["folderImapUID"] = msg->uid();
    _data["gMsgId"] = to_string(msg->gmailMessageID());

    MessageAttributes attrs = MessageAttributesForMessage(msg);
    _data["unread"] = attrs.unread;
    _data["starred"] = attrs.starred;
    _data["labels"] = attrs.labels;
    
    // inflate the participant fields
    _data["from"] = json::array();
    _data["from"] += MailUtils::contactJSONFromAddress(msg->header()->from());

    map<string, void*> fields = {
        {"to", msg->header()->to()},
        {"cc", msg->header()->cc()},
        {"bcc", msg->header()->bcc()},
    };
    
    for (auto const pair : fields) {
        string field = pair.first;
        Array * arr = (Array *)pair.second;
        _data[field] = json::array();

        if (arr != nullptr) {
            for (int ii = 0; ii < arr->count(); ii ++) {
                Address * addr = (Address *)arr->objectAtIndex(ii);
                _data[field].push_back(MailUtils::contactJSONFromAddress(addr));
            }
        }
    }
}

Message::Message(SQLite::Statement & query) :
    MailModel(query)
{

}

// mutable attributes

bool Message::isUnread() {
    return _data["unread"].get<bool>();
}

void Message::setUnread(bool u) {
    _data["unread"] = u;
}

bool Message::isStarred() {
    return _data["starred"].get<bool>();
}

void Message::setStarred(bool s) {
    _data["starred"] = s;
}

json & Message::folderImapXGMLabels() {
    return _data["labels"];
}

void Message::setFolderImapXGMLabels(json & labels) {
    _data["labels"] = labels;
}

string Message::threadId() {
    return _data["threadId"].get<string>();
}

void Message::setThreadId(string threadId) {
    _data["threadId"] = threadId;
}

string Message::snippet() {
    return _data["snippet"].get<string>();
}

void Message::setSnippet(string s) {
    _data["snippet"] = s;
}

json Message::files() {
    return _data["files"];
}

void Message::setFiles(vector<File> & files) {
    json arr = json::array();
    for (auto & file : files) {
        arr.push_back(file.toJSON());
    }
    _data["files"] = arr;
}

void Message::setBodyForDispatch(string s) {
    _bodyForDispatch = s;
}

uint32_t Message::folderImapUID() {
    return _data["folderImapUID"].get<uint32_t>();
}

void Message::setFolderImapUID(uint32_t v) {
    _data["folderImapUID"] = v;
}

json Message::folder() {
    return _data["folder"];
}

void Message::setFolder(Folder & folder) {
    _data["folder"] = folder.toJSON();
}

// immutable attributes

json & Message::to() {
    return _data["to"];
}

json & Message::cc(){
    return _data["cc"];
}

json & Message::bcc(){
    return _data["bcc"];
}

json & Message::from() {
    return _data["from"];
}

time_t Message::date() {
    return _data["date"].get<time_t>();
}

string Message::subject() {
    return _data["subject"].get<string>();
}

string Message::folderId() {
    return _data["folder"]["id"].get<string>();
}

string Message::gMsgId() {
    return _data["gMsgId"].get<string>();
}

string Message::headerMessageId() {
    return _data["headerMessageId"].get<string>();
}

string Message::tableName() {
    return Message::TABLE_NAME;
}

vector<string> Message::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "headerMessageId", "subject", "gMsgId", "date", "draft", "isSent", "unread", "starred", "folderImapUID", "folderImapXGMLabels", "folderId", "threadId"};
}

void Message::bindToQuery(SQLite::Statement & query) {
    MailModel::bindToQuery(query);
    query.bind(":date", (double)date());
    query.bind(":unread", isUnread());
    query.bind(":starred", isStarred());
    query.bind(":headerMessageId", headerMessageId());
    query.bind(":subject", subject());
    query.bind(":folderImapUID", folderImapUID());
    query.bind(":folderImapXGMLabels", folderImapXGMLabels().dump());
    query.bind(":folderId", folderId());
    query.bind(":threadId", threadId());
    query.bind(":gMsgId", gMsgId());
}

json Message::toJSONDispatch() {
    json j = toJSON();
    if (_bodyForDispatch.length() > 0) {
        j["body"] = _bodyForDispatch;
    }
    return j;
}
