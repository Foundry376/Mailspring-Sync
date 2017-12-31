//
//  Thread.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
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

using namespace nlohmann;
using namespace std;


class Thread : public MailModel {
    
    time_t _initialLMST;
    time_t _initialLMRT;
    map<string, bool> _initialCategoryIds;
    
public:
    static string TABLE_NAME;

    Thread(string msgId, string accountId, string subject, uint64_t gThreadId);
    Thread(SQLite::Statement & query);
    
    bool supportsMetadata();

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
    json & labels();
    string categoriesSearchString();

    void resetCountedAttributes();
    void applyMessageAttributeChanges(MessageSnapshot & old, Message * next, vector<shared_ptr<Label>> allLabels);
    void upsertReferences(SQLite::Database & db, string headerMessageId, mailcore::Array * references);

    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);

private:
    map<string, bool> captureCategoryIDs();
    void captureInitialState();
    void addMissingParticipants(std::map<std::string, bool> & existing, json & incoming);

};

#endif /* Thread_hpp */
