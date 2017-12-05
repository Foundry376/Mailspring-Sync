//
//  MailProcessor.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MailProcessor_hpp
#define MailProcessor_hpp

#include <stdio.h>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "spdlog/spdlog.h"
#include "json.hpp"

#include "Folder.hpp"
#include "Message.hpp"
#include "Query.hpp"
#include "Thread.hpp"
#include "Contact.hpp"
#include "Account.hpp"

#include "MailStore.hpp"

using namespace mailcore;
using namespace std;

class MailProcessor {
    MailStore * store;
    shared_ptr<Account> account;
    shared_ptr<spdlog::logger> logger;

public:
    MailProcessor(shared_ptr<Account> account, MailStore * store);
    shared_ptr<Message> insertFallbackToUpdateMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp);
    shared_ptr<Message> insertMessage(IMAPMessage * mMsg, Folder & folder, time_t syncDataTimestamp);
    void updateMessage(Message * local, IMAPMessage * remote, Folder & folder, time_t syncDataTimestamp);
    void retrievedMessageBody(Message * message, MessageParser * parser);
    bool retrievedFileData(File * file, Data * data);
    void unlinkMessagesMatchingQuery(Query & query, int phase);
    void deleteMessagesStillUnlinkedFromPhase(int phase);
    
private:
    void appendToThreadSearchContent(Thread * thread, Message * messageToAppendOrNull, String * bodyToAppendOrNull);
    void upsertThreadReferences(string threadId, string accountId, string headerMessageId, Array * references);
    void upsertContacts(Message * message);
    shared_ptr<Label> labelForXGMLabelName(string mlname);
};

#endif /* MailProcessor_hpp */
