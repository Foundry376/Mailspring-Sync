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

#include "json.hpp"

using json = nlohmann::json;

class Thread : public MailModel {
    
    int _unread;
    int _starred;
    double _firstMessageDate;
    double _lastMessageDate;
    double _lastMessageReceivedDate;
    double _lastMessageSentDate;
    std::string _subject;
    
public:
    static std::string TABLE_NAME;

    Thread(SQLite::Statement & query);
    Thread(Message msg);

    std::string subject();
    void setSubject(std::string s);
    int unread();
    void setUnread(int u);
    int starred();
    void setStarred(int s);
    void addMessage(Message & msg);
    void upsertReferences(SQLite::Database & db, std::string headerMessageId, mailcore::Array * references);

    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);

    json toJSON();
};

#endif /* Thread_hpp */
