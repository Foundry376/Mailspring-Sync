
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

Message::Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp) :
MailModel(MailUtils::idForMessage(folder.accountId(), msg), folder.accountId(), 0)
{
    _data["_sa"] = syncDataTimestamp;
    _data["_suc"] = 0;
    
    setClientFolder(folder);
    setRemoteFolder(folder);

    _data["remoteUID"] = msg->uid();
    
    _data["files"] = json::array();
    _data["date"] = msg->header()->date();
    _data["hMsgId"] = msg->header()->messageID()->UTF8Characters();
    _data["subject"] = msg->header()->subject() ? msg->header()->subject()->UTF8Characters() : "No Subject";
    _data["gMsgId"] = to_string(msg->gmailMessageID());

    MessageAttributes attrs = MessageAttributesForMessage(msg);
    _data["unread"] = attrs.unread;
    _data["starred"] = attrs.starred;
    _data["labels"] = attrs.labels;
    _data["draft"] = attrs.draft;
    
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

Message::Message(json json) :
    MailModel(json)
{
    if (json.count("id") == 0) {
        _data["id"] = MailUtils::idForDraftHeaderMessageId(headerMessageId());
    }
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

json & Message::remoteXGMLabels() {
    return _data["labels"];
}

void Message::setRemoteXGMLabels(json & labels) {
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

bool Message::isDraft() {
    return _data["draft"].get<bool>();
}

void Message::setDraft(bool d) {
    _data["draft"] = d;
}

void Message::setBodyForDispatch(string s) {
    _bodyForDispatch = s;
}

bool Message::isSentByUser() {
    // returns true if the message is in the sent folder or has the sent label
    if (_data["folder"]["role"].get<string>() == "sent") {
        return true;
    }
    for (auto l : _data["labels"]) {
        if (l.get<string>() == "\\Sent") {
            return true;
        }
    }
    return false;
}

uint32_t Message::remoteUID() {
    return _data["remoteUID"].get<uint32_t>();
}

void Message::setRemoteUID(uint32_t v) {
    _data["remoteUID"] = v;
}

json Message::clientFolder() {
    return _data["folder"];
}

string Message::clientFolderId() {
    return _data["folder"]["id"].get<string>();
}

void Message::setClientFolder(Folder & folder) {
    _data["folder"] = folder.toJSON();
    if (_data["folder"].count("localStatus")) {
        _data["folder"].erase("localStatus");
    }
}

json Message::remoteFolder() {
    return _data["remoteFolder"];
}

string Message::remoteFolderId() {
    return _data["remoteFolder"]["id"].get<string>();
}

void Message::setRemoteFolder(Folder & folder) {
    _data["remoteFolder"] = folder.toJSON();
    if (_data["remoteFolder"].count("localStatus")) {
        _data["remoteFolder"].erase("localStatus");
    }
}

time_t Message::syncedAt() {
    return _data["_sa"].get<time_t>();
}

void Message::setSyncedAt(time_t t) {
    _data["_sa"] = t;
}

int Message::syncUnsavedChanges() {
    return _data["_suc"].get<int>();
}

void Message::setSyncUnsavedChanges(int t) {
    _data["_suc"] = t;
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

string Message::gMsgId() {
    return _data["gMsgId"].get<string>();
}

string Message::headerMessageId() {
    return _data["hMsgId"].get<string>();
}

string Message::tableName() {
    return Message::TABLE_NAME;
}

vector<string> Message::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "headerMessageId", "subject", "gMsgId", "date", "draft", "unread", "starred", "remoteUID", "remoteXGMLabels", "remoteFolderId", "threadId"};
}

void Message::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":date", (double)date());
    query->bind(":unread", isUnread());
    query->bind(":starred", isStarred());
    query->bind(":draft", isDraft());
    query->bind(":headerMessageId", headerMessageId());
    query->bind(":subject", subject());
    query->bind(":remoteUID", remoteUID());
    query->bind(":remoteXGMLabels", remoteXGMLabels().dump());
    query->bind(":remoteFolderId", remoteFolderId());
    query->bind(":threadId", threadId());
    query->bind(":gMsgId", gMsgId());
}

json Message::toJSONDispatch() {
    json j = toJSON();
    if (_bodyForDispatch.length() > 0) {
        j["body"] = _bodyForDispatch;
    }
    return j;
}
