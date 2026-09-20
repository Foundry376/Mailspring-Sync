
//  Message.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Message.hpp"
#include "MailStore.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"
#include "File.hpp"
#include "Thread.hpp"
#include "Placement.hpp"

using namespace std;

string Message::TABLE_NAME = "Message";

static int flagBitsFor(bool unread, bool starred, bool draft) {
    return (unread ? PLACEMENT_FLAG_UNREAD : 0)
         | (starred ? PLACEMENT_FLAG_STARRED : 0)
         | (draft ? PLACEMENT_FLAG_DRAFT : 0);
}

/*
 A deleted draft is replaced by an invisible placeholder message that takes over the
 draft's (Drafts, UID) placement until the deletion reaches the server. Without it the
 Drafts scan would see a UID it does not know and put the draft back. (If you delete a
 lot of drafts and the deletion queue is long, the delay is long enough for that.)
 The placeholder has a fresh id and headerMessageId so the draft's own can be reused
 immediately. The caller moves the placements over; the placeholder starts with none.
*/
shared_ptr<Message> Message::messageWithDeletionPlaceholderFor(shared_ptr<Message> draft) {
    json stubJSON = draft->toJSON(); // note: copy
    stubJSON["id"] = "deleted-" + MailUtils::idRandomlyGenerated();
    stubJSON["hMsgId"] = "deleted-" + stubJSON["id"].get<string>();
    stubJSON["subject"] = "Deleting...";
    stubJSON["folders"] = json::object();
    stubJSON["labels"] = json::array();
    
    // very important to set v=0 so the Message gets both "added" and "deleted"
    // from the thread. Otherwise we could potentially cause double deletion
    // and get the counters off by one!
    stubJSON["v"] = 0;
    
    auto stub = make_shared<Message>(stubJSON);
    stub->setDraft(false);
    stub->setUnread(false);
    stub->setStarred(false);
    stub->setSyncUnsavedChanges(1);
    stub->setSyncedAt(time(0) + 1 * 60 * 60);

    return stub;
}

Message::Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp) :
MailModel(MailUtils::idForMessage(folder.accountId(), folder.path(), msg), folder.accountId(), 0)
{
    _skipThreadUpdatesAfterSave = false;
    _lastSnapshot = MessageEmptySnapshot;
    _data["_sa"] = syncDataTimestamp;
    _data["_suc"] = 0;
    
    _data["files"] = json::array();
    _data["date"] = msg->header()->date() == -1 ? msg->header()->receivedDate() : msg->header()->date();
    _data["hMsgId"] = msg->header()->messageID() ? msg->header()->messageID()->UTF8Characters() : "no-header-message-id";
    _data["subject"] = msg->header()->subject() ? msg->header()->subject()->UTF8Characters() : "No Subject";
    _data["gMsgId"] = to_string(msg->gmailMessageID());
    
    Array * irt = msg->header()->inReplyTo();
    if (irt && irt->count() && irt->lastObject()) {
        _data["rthMsgId"] = ((String*)irt->lastObject())->UTF8Characters();
    } else {
        _data["rthMsgId"] = nullptr;
    }

    MessageAttributes attrs = MessageAttributesForMessage(msg);
    _data["unread"] = attrs.unread;
    _data["starred"] = attrs.starred;
    _data["labels"] = attrs.labels;
    _data["draft"] = attrs.draft;
    if (folder.role() == "drafts") {
        _data["draft"] = true;
    }
    _data["folders"] = json::object();
    _data["folders"][folder.id()] = flagBitsFor(attrs.unread, attrs.starred, _data["draft"].get<bool>());

    _data["extraHeaders"] = json::object();
    auto extra = msg->header()->allExtraHeadersNames();
    for (unsigned int ii = 0; ii < extra->count(); ii ++) {
        auto const key = (String *)extra->objectAtIndex(ii);
        if (key == nullptr) continue;
        auto const val = msg->header()->extraHeaderValueForName(key);
        if (val == nullptr) continue;
        _data["extraHeaders"][key->UTF8Characters()] = val->UTF8Characters();
    }
    
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
            for (unsigned int ii = 0; ii < arr->count(); ii ++) {
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

    // Client-authored draft JSON carries no "folders"; the engine assigns placements.
    if (!_data.count("folders") || !_data["folders"].is_object()) {
        _data["folders"] = json::object();
    }
    if (!_data.count("labels") || !_data["labels"].is_array()) {
        _data["labels"] = json::array();
    }

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
    s.fileCount = fileCountForThreadList();
    s.labels = labels();
    s.folders = folders();
    return s;
}

void Message::captureSnapshot() {
    _lastSnapshot = getSnapshot();
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

json & Message::folders() {
    if (!_data["folders"].is_object()) {
        _data["folders"] = json::object();
    }
    return _data["folders"];
}

vector<string> Message::folderIds() {
    vector<string> ids;
    for (auto it = folders().begin(); it != folders().end(); ++it) {
        ids.push_back(it.key());
    }
    return ids;
}

string Message::folderRole(MailStore * store, string folderId) {
    auto folder = store->folderById(accountId(), folderId);
    return folder == nullptr ? "" : folder->role();
}

// A message is "in all mail" when at least one of its copies is somewhere other than
// spam or trash. A message with no live copies (in transit between folders) is not.
bool Message::inAllMail(MailStore * store) {
    for (auto & folderId : folderIds()) {
        string role = folderRole(store, folderId);
        if (role != "spam" && role != "trash") {
            return true;
        }
    }
    return false;
}

bool Message::isUnread() {
    return _data["unread"].get<bool>();
}

// The flag setters write only the derived message-level value. Per-copy bits live on
// the placements and reach "folders" through the MailStore helpers.

void Message::setUnread(bool u) {
    _data["unread"] = u;
}

bool Message::isStarred() {
    return _data["starred"].get<bool>();
}

void Message::setStarred(bool s) {
    _data["starred"] = s;
}

json & Message::labels() {
    return _data["labels"];
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

bool Message::plaintext() {
    return _data["plaintext"].get<bool>();
}

void Message::setPlaintext(bool p) {
    _data["plaintext"] = p;
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

/* Mailspring displays the "attachment" icon only if the following criteria are met.
   To make search and the thread list consistent, I'm moving the impl here to C++.
 
 !f.contentId || f.size > 12 * 1024
 */
int Message::fileCountForThreadList() {
    if (!files().is_array()) return 0;
    
    int count = 0;
    for (auto & file : files()) {
        if (file["contentId"].is_null() || file["size"].get<int>() > 12 * 1024) {
            count ++;
        }
    }

    return count;
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

bool Message::isSentByUser(MailStore * store) {
    return this->_isIn(store, "sent");
}

bool Message::isInInbox(MailStore * store) {
    return this->_isIn(store, "inbox");
}

// True when any copy is in a folder with the given role, or (Gmail) in All Mail
// carrying a label whose name contains it.
bool Message::_isIn(MailStore * store, string roleAlsoLabelName) {
    for (auto & folderId : folderIds()) {
        string role = folderRole(store, folderId);
        if (role == roleAlsoLabelName) {
            return true;
        }
        if (role != "all") {
            continue;
        }
        string needle = roleAlsoLabelName;
        for (auto & l : labels()) {
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
    return vector<string>{"id", "data", "accountId", "version", "headerMessageId", "subject", "gMsgId", "date", "draft", "unread", "starred", "threadId"};
}

void Message::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":date", (double)date());
    query->bind(":unread", isUnread());
    query->bind(":starred", isStarred());
    query->bind(":draft", isDraft());
    query->bind(":headerMessageId", headerMessageId());
    query->bind(":subject", subject());
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
    auto thread = store->find<Thread>(Query().equal("id", threadId()));
    if (thread == nullptr) {
        return;
    }

    thread->applyMessageAttributeChanges(_lastSnapshot, this, store);
    store->save(thread.get());
    _lastSnapshot = getSnapshot();
}

void Message::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);

    store->deletePlacementsForMessage(id());

    // if we have a thread, keep the thread's folder, label, and unread counters
    // in sync by providing it with a before + after snapshot of this message.
    if (threadId() != "") {
        auto thread = store->find<Thread>(Query().equal("id", threadId()));
        if (thread != nullptr) {
            thread->applyMessageAttributeChanges(_lastSnapshot, nullptr, store);
            if (thread->folders().size() == 0) {
                store->remove(thread.get());
            } else {
                store->save(thread.get());
            }
        }
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
        j["fullSyncComplete"] = true;
    }
    // We need to store this attribute because deltas can be flattened / concatenated together
    // in the buffer before they're sent to the client, so it's possible for the client to only
    // get V2 if bodies sync rapidly.
    if (version() == 1) {
        j["headersSyncComplete"] = true;
    }
    return j;
}
