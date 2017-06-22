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

#include "MailStore.hpp"

using namespace mailcore;
using namespace std;

class MailProcessor {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

public:
    MailProcessor(MailStore * store);
    void insertMessage(IMAPMessage * mMsg, Folder & folder);
    void updateMessage(Message * local, IMAPMessage * remote, Folder & folder);
    void unlinkMessagesFromFolder(vector<shared_ptr<Message>> localMessages);

private:
    void upsertThreadReferences(string threadId, string headerMessageId, Array * references);
    shared_ptr<Label> labelForXGMLabelName(string mlname);
};

#endif /* MailProcessor_hpp */
