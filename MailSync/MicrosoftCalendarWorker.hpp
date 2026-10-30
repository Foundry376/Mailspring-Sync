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
#include "SyncException.hpp"

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

    json fetchCalendar(const string & calendarId);
    bool organizedElsewhere(const string & ics);
    vector<shared_ptr<Event>> findRows(shared_ptr<Event> event);
    shared_ptr<Event> findInvitation(const string & uid);
    void removeRows(const vector<shared_ptr<Event>> & rows);
    json fetchEvent(const string & calendarPath, const string & graphId);
    json fetchSeries(const string & calendarPath, const string & graphId);
    vector<json> findOccurrences(const string & calendarPath, const json & series, const vector<ICSProperty> & anchors,
                                 vector<SyncException> & failures);
    void writeOccurrences(const string & calendarPath, const string & graphId, bool guest, const vector<GraphEventChange> & changes);
    string storeEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows);
    void restoreEvent(const json & calendar, const string & graphId, const vector<shared_ptr<Event>> & rows);

public:
    MicrosoftCalendarWorker(shared_ptr<Account> account);

    void run();

    // Makes Graph's copy of `event` match `ics`, the client's edit of `previousIcs` (unused for an
    // event Graph doesn't hold yet), then stores Graph's copy in its rows. Returns its master row's id.
    string writeEvent(shared_ptr<Event> event, const string & previousIcs, const string & ics);

    // Answers an invitation through Graph, which tells its organizer: for the occurrence the iTIP `reply`
    // names, else the whole event. False, having sent nothing, when none of the user's own calendars holds its UID.
    bool answerEvent(const string & reply, const string & partstat);

    // Deletes the event, or the whole series of a master row, and its rows. A guest declines it.
    void deleteEvent(shared_ptr<Event> event);
};

#endif /* MicrosoftCalendarWorker_hpp */
