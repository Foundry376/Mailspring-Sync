
//  Message.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Message.hpp"
#include "MailStore.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"
#include "MailStore.hpp"
#include "File.hpp"
#include "Thread.hpp"

using namespace std;

string Message::TABLE_NAME = "Message";

/*
 The concept behind the "deletion placeholder" is that we need something 
 in the database with the remoteFolder and remoteUID of the message until
 we finish syncing the deletion to the server. Otherwise the sync worker 
 could put it back. (If you try to delete a lot of drafts and the deletion
 queue is long, the delay can be long enough for them to reappear.) Bad!

 In this approach we "free" up the headerMessageId and id, and create an
 invisible message for a few seconds.
*/
shared_ptr<Message> Message::messageWithDeletionPlaceholderFor(shared_ptr<Message> draft) {
    json stubJSON = draft->toJSON(); // note: copy
    stubJSON["id"] = "deleted-" + MailUtils::idRandomlyGenerated();
    stubJSON["hMsgId"] = "deleted-" + stubJSON["id"].get<string>();
    stubJSON["subject"] = "Deleting...";
    
    // very important to set v=0 so the Message gets both "added" and "deleted"
    // from the thread. Otherwise we could potentially cause double deletion
    // and get the counters off by one!
    stubJSON["v"] = 0;
    
    auto stub = make_shared<Message>(stubJSON);
    auto nolabels = json::array();
    stub->setDraft(false);
    stub->setUnread(false);
    stub->setStarred(false);
    stub->setRemoteXGMLabels(nolabels);
    stub->setSyncUnsavedChanges(1);
    stub->setSyncedAt(time(0) + 1 * 60 * 60);

    return stub;
}

Message::Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp) :
MailModel(MailUtils::idForMessage(folder.accountId(), msg), folder.accountId(), 0)
{
    _skipThreadUpdatesAfterSave = false;
    _lastSnapshot = MessageEmptySnapshot;
    _data["_sa"] = syncDataTimestamp;
    _data["_suc"] = 0;
    
    setClientFolder(&folder);
    setRemoteFolder(&folder);

    _data["remoteUID"] = msg->uid();
    
    _data["files"] = json::array();
    _data["date"] = msg->header()->date();
    _data["hMsgId"] = msg->header()->messageID() ? msg->header()->messageID()->UTF8Characters() : "no-header-message-id";
    _data["subject"] = msg->header()->subject() ? msg->header()->subject()->UTF8Characters() : "No Subject";
    _data["gMsgId"] = to_string(msg->gmailMessageID());
    
    Array * irt = msg->header()->inReplyTo();
    if (irt && irt->count() && irt->lastObject()) {
        _data["rtMsgId"] = ((String*)irt->lastObject())->UTF8Characters();
    } else {
        _data["rtMsgId"] = nullptr;
    }

    MessageAttributes attrs = MessageAttributesForMessage(msg);
    _data["unread"] = attrs.unread;
    _data["starred"] = attrs.starred;
    _data["labels"] = attrs.labels;
    _data["draft"] = attrs.draft;
    
    // inflate the participant fields
    _data["from"] = json::array();
    if (msg->header()->from()) {
        _data["from"] += MailUtils::contactJSONFromAddress(msg->header()->from());
    }

    map<string, void*> fields = {
        {"to", msg->header()->to()},
        {"cc", msg->header()->cc()},
        {"bcc", msg->header()->bcc()},
        {"replyTo", msg->header()->replyTo()},
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
    _skipThreadUpdatesAfterSave = false;
    _lastSnapshot = getSnapshot();
}

Message::Message(json json) :
    MailModel(json)
{
    _skipThreadUpdatesAfterSave = false;
    if (version() == 0) {
        _lastSnapshot = MessageEmptySnapshot;
    } else {
        _lastSnapshot = getSnapshot();
    }
}

MessageSnapshot Message::getSnapshot() {
    MessageSnapshot s;
    s.unread = isUnread();
    s.starred = isStarred();
    s.fileCount = files().size();
    s.remoteXGMLabels = remoteXGMLabels();
    s.clientFolderId = clientFolderId();
    return s;
}

bool Message::supportsMetadata() {
    return true;
}

bool Message::isDeletionPlaceholder() {
    return id().substr(0, 8) == "deleted-";
}

bool Message::isHiddenReminder() {
    // check for "Ben via Mailspring" reminders / unsnoozes
    if (from().size() != 1) {
        return false;
    }
    if (!from()[0].count("name") || !from()[0]["name"].is_string()) {
        return false;
    }
    auto fromName = from()[0]["name"].get<string>();
    if (fromName.length() < 15) {
        return false;
    }
    auto fromEnd = fromName.substr(fromName.length() - 14, 14);
    return (fromEnd == "via Mailspring");
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

string Message::replyToHeaderMessageId() {
    if (_data["rthMsgId"].is_null()) {
        return "";
    }
    return _data["rthMsgId"].get<string>();
}

void Message::setReplyToHeaderMessageId(string s) {
    _data["rthMsgId"] = s;
}

string Message::forwardedHeaderMessageId() {
    if (_data["fwdMsgId"].is_null()) {
        return "";
    }
    return _data["fwdMsgId"].get<string>();
}

void Message::setForwardedHeaderMessageId(string s) {
    _data["fwdMsgId"] = s;
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
    return this->_isIn("sent");
}

bool Message::isInInbox() {
    return this->_isIn("inbox");
}

bool Message::_isIn(string roleAlsoLabelName) {
    string folderRole = remoteFolder()["role"].get<string>();
    if (folderRole == roleAlsoLabelName) {
        return true;
    }
    if (folderRole == "all") {
        string needle = roleAlsoLabelName;
        for (auto & l : remoteXGMLabels()) {
            string ln = l.get<string>();
            auto it = std::search(ln.begin(), ln.end(), needle.begin(), needle.end(), [](char ch1, char ch2) {
                return std::toupper(ch1) == std::toupper(ch2);
            });
            if (it != ln.end()) {
                return true;
            }
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

void Message::setClientFolder(Folder * folder) {
    _data["folder"] = folder->toJSON();
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

void Message::setRemoteFolder(json folder) {
    _data["remoteFolder"] = folder;
}

void Message::setRemoteFolder(Folder * folder) {
    _data["remoteFolder"] = folder->toJSON();
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

json & Message::replyTo(){
    return _data["replyTo"];
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

void Message::afterSave(MailStore * store) {
    MailModel::afterSave(store);

    // if we have a thread, keep the thread's folder, label, and unread counters
    // in sync by providing it with a before + after snapshot of this message.
    if (_skipThreadUpdatesAfterSave) {
        return;
    }
    if (threadId() == "") {
        return;
    }
    auto thread = store->find<Thread>(Query().equal("accountId", accountId()).equal("id", threadId()));
    if (thread == nullptr) {
        return;
    }

    auto allLabels = store->allLabelsCache(accountId());
    thread->applyMessageAttributeChanges(_lastSnapshot, this, allLabels);
    store->save(thread.get());
    _lastSnapshot = getSnapshot();
}

void Message::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);
    
    // if we have a thread, keep the thread's folder, label, and unread counters
    // in sync by providing it with a before + after snapshot of this message.
    
    if (threadId() == "") {
        return;
    }
    auto thread = store->find<Thread>(Query().equal("accountId", accountId()).equal("id", threadId()));
    if (thread == nullptr) {
        return;
    }
    
    auto allLabels = store->allLabelsCache(accountId());
    thread->applyMessageAttributeChanges(_lastSnapshot, nullptr, allLabels);
    if (thread->folders().size() == 0) {
        store->remove(thread.get());
    } else {
        store->save(thread.get());
    }
    
    // Also delete our draft body
    SQLite::Statement removeBody(store->db(), "DELETE FROM MessageBody WHERE id = ?");
    removeBody.bind(1, id());
    removeBody.exec();
}

json Message::toJSONDispatch() {
    json j = toJSON();
    if (_bodyForDispatch.length() > 0) {
        j["body"] = _bodyForDispatch;
    }
    return j;
}
