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

class MailProcessor {
    MailStore * store;
    std::shared_ptr<spdlog::logger> logger;

public:
    MailProcessor(MailStore * store);
    void insertMessage(IMAPMessage * mMsg, Folder & folder);
    void upsertThreadReferences(std::string threadId, std::string headerMessageId, Array * references);
};

#endif /* MailProcessor_hpp */
