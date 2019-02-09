//
//  Event.cpp
//  MailSync
//
//  Created by Ben Gotow on 2/08/19.
//  Copyright © 2019 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "Event.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"
#include "icalendar.h"

using namespace std;
using namespace mailcore;

string Event::TABLE_NAME = "Event";

static Date DISTANT_FUTURE;

Date endOf(ICalendarEvent * event) {
    if (event->RRule.IsEmpty()) {
        return event->DtEnd;
    }
    if (!event->RRule.Until.IsEmpty()) {
        return event->RRule.Until;
    }
    if (event->RRule.Count > 0) {
        // TODO BEN
    }

    DISTANT_FUTURE = "20400413T175959Z";
    return DISTANT_FUTURE;
}

Event::Event(string etag, string accountId, string calendarId, string icsData) : MailModel(etag, accountId) {
    _data["cid"] = calendarId;

    // Build our start and end time from the ics data. These values represent the time range in which
    // the event needs to be considered for display, so we include the entire time the event is recurring.
    ICalendar cal(icsData);
    auto calEvent = cal.Events.front();
    _data["_start"] = calEvent->DtStart.unix();
    _data["_end"] = endOf(calEvent).unix();
    _data["ics"] = icsData;
}

Event::Event(SQLite::Statement & query) :
    MailModel(query)
{
}

string Event::constructorName() {
    return _data["__cls"].get<string>();
}

string Event::tableName() {
    return Event::TABLE_NAME;
}

string Event::calendarId() {
    return _data["cid"].get<string>();
}

string Event::icsData() {
    return _data["ics"].get<string>();
}

int Event::_start() {
    return _data["_start"].get<int>();
}

int Event::_end() {
    return _data["_end"].get<int>();
}

vector<string> Event::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "calendarId", "_start", "_end"};
}

void Event::bindToQuery(SQLite::Statement * query) {
    query->bind(":id", id());
    query->bind(":data", this->toJSON().dump());
    query->bind(":accountId", accountId());
    query->bind(":calendarId", calendarId());
    query->bind(":_start", _start());
    query->bind(":_end", _end());
}
