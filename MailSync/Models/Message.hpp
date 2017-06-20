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

class Message : public MailModel {
    bool _unread;
    bool _starred;
    double _date;
    uint32_t _folderImapUID;
    
    std::string _folderId;
    std::string _threadId;
    std::string _headerMessageId;
    std::string _subject;
    std::string _gMsgId;
    
public:
    Message(mailcore::IMAPMessage * msg, Folder & folder);
    Message(SQLite::Statement & query);
    
    bool isUnread();
    bool isStarred();
    double date();
    std::string subject();
    
    void setThreadId(std::string threadId);
    std::string getHeaderMessageId();
    
    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);
    
    void updateAttributes(mailcore::IMAPMessage * msg);
    
    json toJSON();
};

#endif /* Message_hpp */
