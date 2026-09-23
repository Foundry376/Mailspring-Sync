//
//  Message.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Message_hpp
#define Message_hpp

#include <stdio.h>
#include <vector>
#include <string>
#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "MailModel.hpp"
#include "Folder.hpp"

#include "json.hpp"

using namespace std;
using namespace nlohmann;

class File;
class MailStore;
class Message;

// Snapshot concept
//
// The state of a message that contributes to its thread's counters, captured when the
// message is loaded and compared against the message after a save so the thread can be
// updated by diff. `folders` is the { folderId: flagBits } map from the message JSON, so
// capturing it costs no query.

struct MessageSnapshot {
    bool unread;
    bool starred;
    size_t fileCount;
    json labels;
    json folders;
};

static MessageSnapshot MessageEmptySnapshot = MessageSnapshot{false, false, 0, json::array(), json::object()};

// Message

class Message : public MailModel {

    string _bodyForDispatch;
    MessageSnapshot _lastSnapshot;

public:
    static string TABLE_NAME;
    
    static shared_ptr<Message> messageWithDeletionPlaceholderFor(shared_ptr<Message> draft);

    Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp);
    Message(SQLite::Statement & query);
    Message(json json);
    
    bool supportsMetadata();

    // mutable attributes

    MessageSnapshot getSnapshot();
    void captureSnapshot();

    bool isDeletionPlaceholder();
    bool isHiddenReminder();

    // Placement snapshot: { "<folderId>": bits }, live copies only (see Placement.hpp).
    // Rebuilt from the rows on save (see placementsChanged); read by the client and Thread.
    json & folders();
    vector<string> folderIds();
    string folderRole(MailStore * store, string folderId);

    bool inAllMail(MailStore * store);

    bool isUnread();
    void setUnread(bool u);

    bool isStarred();
    void setStarred(bool s);
    
    string threadId();
    void setThreadId(string threadId);

    string snippet();
    void setSnippet(string s);
    
    bool plaintext();
    void setPlaintext(bool p);

    string replyToHeaderMessageId();
    void setReplyToHeaderMessageId(string s);
    
    string forwardedHeaderMessageId();
    void setForwardedHeaderMessageId(string s);

    json files();
    void setFiles(vector<File> & files);
    int fileCountForThreadList();

    bool isDraft();
    void setDraft(bool d);
    
    time_t syncedAt();
    void setSyncedAt(time_t t);
    
    int syncUnsavedChanges();
    void setSyncUnsavedChanges(int t);
    
    void setBodyForDispatch(string s);

    bool isSentByUser(MailStore * store);
    bool isInInbox(MailStore * store);
    bool _isIn(MailStore * store, string roleAlsoLabelName);

    // X-GM-LABELS of the message's live copies (Gmail has one). Rebuilt alongside
    // "folders"; the client reads it as `labels`.
    json & labels();

    // immutable attributes

    json & to();
    json & cc();
    json & bcc();
    json & from();
    json & replyTo();
    
    time_t date();
    string subject();
    string gMsgId();
    string headerMessageId();
    
    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void beforeSave(MailStore * store);
    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

    json toJSONDispatch();

    bool _skipThreadUpdatesAfterSave;

    // Set by the MailStore placement helpers when they change this message's rows. The
    // save rebuilds the snapshot from the rows (MailStore::refreshMessageFromPlacements),
    // which clears it. In memory only.
    bool placementsChanged();
    void setPlacementsChanged(bool changed);

private:
    bool _placementsChanged;
};

#endif /* Message_hpp */
