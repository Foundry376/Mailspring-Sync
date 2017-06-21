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
    bool _unread;
    bool _starred;
    double _date;
    uint32_t _folderImapUID;
    
    string _folderId;
    string _threadId;
    string _headerMessageId;
    string _subject;
    string _gMsgId;
    
public:
    static string TABLE_NAME;

    Message(mailcore::IMAPMessage * msg, Folder & folder);
    Message(SQLite::Statement & query);
    
    bool isUnread();
    bool isStarred();
    double date();
    string subject();
    
    void setThreadId(string threadId);
    string getHeaderMessageId();
    
    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);
    
    void updateAttributes(mailcore::IMAPMessage * msg);
    
    json toJSON();
};

#endif /* Message_hpp */
