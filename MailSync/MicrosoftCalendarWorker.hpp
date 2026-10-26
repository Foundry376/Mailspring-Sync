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
#include "Event.hpp"
#include "MailStore.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

class MicrosoftCalendarWorker {
    unique_ptr<MailStore> store;
    shared_ptr<spdlog::logger> logger;
    shared_ptr<Account> account;

    json graphGET(const string & url, const string & accessToken, const vector<string> & headers = {});
    void paginateGraphCollection(const string & url, const string & accessToken, std::function<void(json &)> yieldPage);

    void upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote);
    void syncEvents(const string & calendarId, const string & path, const string & accessToken);
    json fetchEvent(const json & entry, const string & accessToken);
    void ingestEvent(const string & calendarId, const json & entry, const string & ics,
                     const vector<shared_ptr<Event>> & previousRows);

public:
    MicrosoftCalendarWorker(shared_ptr<Account> account);

    void run();
};

#endif /* MicrosoftCalendarWorker_hpp */
