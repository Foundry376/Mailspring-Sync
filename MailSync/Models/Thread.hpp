//
//  Thread.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Thread_hpp
#define Thread_hpp

#include <stdio.h>
#include <vector>
#include <string>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "MailModel.hpp"
#include "Message.hpp"

class Thread : public MailModel {
    
public:
    int _unread;
    int _starred;
    double _firstMessageDate;
    double _lastMessageDate;
    double _lastMessageReceivedDate;
    double _lastMessageSentDate;
    
    Thread(SQLite::Statement & query);
    Thread(Message msg);
    int unread();
    void setUnread(int u);
    int starred();
    void setStarred(int s);
    void addMessage(Message & msg);
    void upsertReferences(SQLite::Database & db, std::string headerMessageId, mailcore::Array * references);

    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);
};

#endif /* Thread_hpp */
