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
#include "Label.hpp"
#include "Message.hpp"

#include "json.hpp"

using json = nlohmann::json;
using namespace std;


class Thread : public MailModel {
    
    bool _initialIsUnread;
    vector<string> _initialCategoryIds;

public:
    static string TABLE_NAME;

    Thread(SQLite::Statement & query);
    Thread(Message msg, uint64_t gThreadId, vector<shared_ptr<Label>> & allLabels);
    
    string subject();
    void setSubject(string s);
    int unread();
    void setUnread(int u);
    int starred();
    void setStarred(int s);
    int attachmentCount();
    void setAttachmentCount(int s);
    
    uint64_t searchRowId();
    void setSearchRowId(uint64_t s);

    json & participants();
    string gThrId();
    bool inAllMail();
    time_t lastMessageTimestamp();
    time_t firstMessageTimestamp();
    time_t lastMessageReceivedTimestamp();
    time_t lastMessageSentTimestamp();

    json & folders();
    void setFolders(json folders);
    
    json & labels();

    void addMessage(Message * msg, vector<shared_ptr<Label>> & allLabels);
    void prepareToReaddMessage(Message * msg, vector<shared_ptr<Label>> & allLabels);

    void upsertReferences(SQLite::Database & db, string headerMessageId, mailcore::Array * references);

    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);
    void writeAssociations(SQLite::Database & db);

private:
    void captureInitialState();
    void addMissingParticipants(std::map<std::string, bool> & existing, json & incoming);

};

#endif /* Thread_hpp */
