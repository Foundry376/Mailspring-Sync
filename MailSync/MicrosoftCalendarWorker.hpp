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
#include "GraphICS.hpp"
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
    string mailboxAddress; // the default calendar's owner, set by listCalendars

    std::function<string()> tokenProvider();

    vector<json> listCalendars();
    vector<string> answeringFor(const json & calendar);
    void upsertCalendar(shared_ptr<Calendar> local, const string & id, const string & path, const json & remote);
    void syncEvents(const json & calendar);
    vector<pair<json, json>> fetchEvents(const string & calendarPath, const vector<json> & entries);
    string ingestEvent(const string & calendarId, const json & entry, const string & ics,
                       const vector<shared_ptr<Event>> & previousRows);

    json listedCalendar(const string & calendarId);
    bool organizedElsewhere(const string & ics);
    vector<shared_ptr<Event>> rowsOf(shared_ptr<Event> event);
    json listedEvent(const string & calendarPath, const string & graphId);
    json readSeries(const string & calendarPath, const string & graphId);
    json occurrenceOf(const string & calendarPath, const json & series, const ICSProperty & anchor);
    string storeEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows);
    void restoreEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows);

public:
    MicrosoftCalendarWorker(shared_ptr<Account> account);

    void run();

    // Makes Graph's copy of `event` match `ics`, the client's edit of `previousIcs` (unused for an
    // event Graph doesn't hold yet), then stores Graph's copy in its rows. Returns its master row's
    // id. An `answer` to an invitation writes nothing: it reaches the organizer by email.
    string writeEvent(shared_ptr<Event> event, const string & previousIcs, const string & ics, bool answer);

    // Deletes the event, or the whole series of a master row, and its rows. A guest declines it.
    void deleteEvent(shared_ptr<Event> event);
};

#endif /* MicrosoftCalendarWorker_hpp */
