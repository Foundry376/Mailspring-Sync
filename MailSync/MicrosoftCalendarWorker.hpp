//
//  MicrosoftCalendarWorker.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MicrosoftCalendarWorker_hpp
#define MicrosoftCalendarWorker_hpp

#include "Account.hpp"
#include "Calendar.hpp"
#include "MailStore.hpp"

#include <functional>
#include <memory>
#include <string>

#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

// Syncs Outlook.com and Office 365 calendars through Microsoft Graph, which has no CalDAV.
class MicrosoftCalendarWorker {
    unique_ptr<MailStore> store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    json graphGET(const string & url, const string & accessToken);
    void paginateGraphCollection(const string & url, const string & accessToken, std::function<void(json &)> yieldPage);

    void upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote);

public:
    MicrosoftCalendarWorker(shared_ptr<Account> account);

    void run();
};

#endif /* MicrosoftCalendarWorker_hpp */
