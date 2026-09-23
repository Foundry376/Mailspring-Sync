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
#include <chrono>

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
    shared_ptr<Message> updateMessage(const string & messageId, IMAPMessage * remote, Folder & folder, time_t syncDataTimestamp);
    void retrievedMessageBody(Message * message, MessageParser * parser);
    bool retrievedFileData(File * file, Data * data);

    // Placement bookkeeping for copies the server no longer reports.
    void deleteVanishedPlacements(Folder & folder, const vector<uint32_t> & uids);
    void deleteVanishedPlacements(Folder & folder, Query & uidQuery);
    void deleteUnassignedPlacements(Folder & folder);
    void sweepExpiredOrphans(time_t before);
    void saveMessagesAfterPlacementChange(const vector<string> & messageIds, bool removeUnplaced = false);
    void detachMessagesFromFolder(string folderId, std::chrono::milliseconds pause = std::chrono::milliseconds(0));
    
private:
    int refreshMessagesInOpenTransaction(const vector<string> & messageIds, bool logSubjects, bool removeUnplaced);
    void saveDisplacedMessage(const string & messageId);
    void appendToThreadSearchContent(Thread * thread, Message * messageToAppendOrNull, String * bodyToAppendOrNull);
    void upsertThreadReferences(string threadId, string accountId, string headerMessageId, Array * references);
    void upsertContacts(Message * message);
    shared_ptr<Label> labelForXGMLabelName(string mlname);
};

#endif /* MailProcessor_hpp */
