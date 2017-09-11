//
//  Message.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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

struct MessageSnapshot {
    bool unread;
    bool starred;
    size_t fileCount;
    json remoteXGMLabels;
    string clientFolderId;
};

static MessageSnapshot MessageEmptySnapshot = MessageSnapshot{false, false, 0, nullptr, ""};

class Message : public MailModel {

    string _bodyForDispatch;
    MessageSnapshot _lastSnapshot;

public:
    static string TABLE_NAME;
    
    Message(mailcore::IMAPMessage * msg, Folder & folder, time_t syncDataTimestamp);
    Message(SQLite::Statement & query);
    Message(json json);
    
    // mutable attributes

    MessageSnapshot getSnapshot();
    
    bool isUnread();
    void setUnread(bool u);

    bool isStarred();
    void setStarred(bool s);
    
    string threadId();
    void setThreadId(string threadId);

    string snippet();
    void setSnippet(string s);

    string replyToHeaderMessageId();
    void setReplyToHeaderMessageId(string s);
    
    json files();
    void setFiles(vector<File> & files);

    bool isDraft();
    void setDraft(bool d);
    
    time_t syncedAt();
    void setSyncedAt(time_t t);
    
    int syncUnsavedChanges();
    void setSyncUnsavedChanges(int t);
    
    void setBodyForDispatch(string s);

    bool isSentByUser();
    bool isInInbox();
    bool _isIn(string roleAlsoLabelName);

    json & remoteXGMLabels();
    void setRemoteXGMLabels(json & labels);

    uint32_t remoteUID();
    void setRemoteUID(uint32_t v);
    
    json clientFolder();
    string clientFolderId();
    void setClientFolder(Folder * folder);
    
    json remoteFolder();
    string remoteFolderId();
    void setRemoteFolder(Folder * folder);

    // immutable attributes

    json & to();
    json & cc();
    json & bcc();
    json & from();
    
    time_t date();
    string subject();
    string gMsgId();
    string headerMessageId();
    
    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

    json toJSONDispatch();

};

#endif /* Message_hpp */
