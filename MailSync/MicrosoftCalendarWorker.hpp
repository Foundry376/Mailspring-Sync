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
#include "NetworkRequestUtils.hpp"

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

    string accessToken();
    [[noreturn]] void throwForGraphResponse(const string & url, const GraphResponse & resp);
    json graphGET(const string & url);
    void paginateGraphCollection(const string & url, std::function<void(json &)> yieldPage);

    void upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote);
    void syncEvents(const string & calendarId, const string & path);
    map<string, json> fetchEvents(const vector<json> & entries);
    void ingestEvent(const string & calendarId, const json & entry, const string & ics,
                     const vector<shared_ptr<Event>> & previousRows);

public:
    MicrosoftCalendarWorker(shared_ptr<Account> account);

    void run();
};

#endif /* MicrosoftCalendarWorker_hpp */
