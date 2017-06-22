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

using json = nlohmann::json;
using namespace std;

class Message : public MailModel {

public:
    static string TABLE_NAME;

    Message(mailcore::IMAPMessage * msg, Folder & folder);
    Message(SQLite::Statement & query);
    
    bool isUnread();
    void setUnread(bool u);

    bool isStarred();
    void setStarred(bool s);
    
    string threadId();
    void setThreadId(string threadId);

    string snippet();
    void setSnippet(string s);

    json & folderImapXGMLabels();
    void setFolderImapXGMLabels(json & labels);

    json & to();
    json & cc();
    json & bcc();
    json & from();
    
    time_t date();
    string subject();
    uint32_t folderImapUID();
    json folder();
    string folderId();
    string gMsgId();
    string headerMessageId();
    
    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);
};

#endif /* Message_hpp */
