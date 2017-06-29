//
//  MailProcessor.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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

#include "MailStore.hpp"

using namespace mailcore;
using namespace std;

class MailProcessor {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

public:
    MailProcessor(string name, MailStore * store);
    void insertFallbackToUpdateMessage(IMAPMessage * mMsg, Folder & folder);
    void insertMessage(IMAPMessage * mMsg, Folder & folder);
    void updateMessage(Message * local, IMAPMessage * remote, Folder & folder);
    void retrievedMessageBody(Message * message, MessageParser * parser);
    bool retrievedFileData(File * file, Data * data);
    void unlinkMessagesFromFolder(vector<shared_ptr<Message>> localMessages, int phase);
    void deleteMessagesStillUnlinkedFromPhase(int phase);
    
private:
    void appendToThreadSearchContent(Thread * thread, Message * messageToAppendOrNull, String * bodyToAppendOrNull);
    void upsertThreadReferences(string threadId, string headerMessageId, Array * references);
    void upsertContacts(Message * message);
    shared_ptr<Label> labelForXGMLabelName(string mlname);
};

#endif /* MailProcessor_hpp */
