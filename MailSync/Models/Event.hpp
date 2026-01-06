//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Event_hpp
#define Event_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "icalendar.h"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"
#include "MailStore.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class Event : public MailModel {
    // Transient search content populated from ICalendarEvent during construction/update.
    // Not persisted - only used during the save lifecycle for EventSearch indexing.
    string _searchTitle;
    string _searchDescription;
    string _searchLocation;
    string _searchParticipants;

public:
    static string TABLE_NAME;

    Event(string etag, string accountId, string calendarId, string ics, ICalendarEvent * event);
    Event(json & data);
    Event(SQLite::Statement & query);

    string etag();
    void setEtag(string etag);

    string calendarId();
    void setCalendarId(string calendarId);

    string icsData();
    void setIcsData(string ics);

    string icsUID();

    string href();
    void setHref(string href);

    string recurrenceId();
    void setRecurrenceId(string recurrenceId);

    string status();
    void setStatus(string status);

    // Apply ICS event data to this event (used by constructor and when updating existing events)
    void applyICSEventData(const string& etag, const string& href,
                           const string& icsData, ICalendarEvent* icsEvent);

    bool isRecurrenceException();

    int recurrenceStart();
    int recurrenceEnd();

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);
};

// Helper function to calculate event end time (handles recurrence)
Date endOf(ICalendarEvent *event);

#endif /* Event_hpp */
