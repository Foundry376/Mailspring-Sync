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

Date endOf(ICalendarEvent *event)
{
    if (event->RRule.IsEmpty())
    {
        return !event->DtEnd.IsEmpty() ? event->DtEnd : event->DtStart;
    }
    if (!event->RRule.Until.IsEmpty())
    {
        return event->RRule.Until;
    }
    if (event->RRule.Count > 0)
    {
        // TODO BEN
    }

    DISTANT_FUTURE = "20370000T000000";
    return DISTANT_FUTURE;
}

Event::Event(string etag, string accountId, string calendarId, string ics, ICalendarEvent *event)
    : MailModel(MailUtils::idForEvent(accountId, calendarId, event->UID, event->RecurrenceId), accountId)
{
    _data["cid"] = calendarId;
    _data["ics"] = ics;
    _data["etag"] = etag;
    _data["icsuid"] = event->UID;

    // Store recurrence exception info - RecurrenceId identifies which occurrence is modified
    _data["rid"] = event->RecurrenceId;
    // Status can be TENTATIVE, CONFIRMED, or CANCELLED
    _data["status"] = event->Status.empty() ? "CONFIRMED" : event->Status;

    // Build our start and end time from the ics data. These values represent the time range in which
    // the event needs to be considered for display, so we include the entire time the event is recurring.
    _data["rs"] = event->DtStart.toUnix();
    _data["re"] = endOf(event).toUnix();
}

Event::Event(json & data) : MailModel(data)
{
    // Event constructed from client JSON - already has all fields
}

Event::Event(SQLite::Statement &query) : MailModel(query)
{
}

string Event::constructorName()
{
    return _data["__cls"].get<string>();
}

string Event::tableName()
{
    return Event::TABLE_NAME;
}

string Event::calendarId()
{
    return _data["cid"].get<string>();
}

void Event::setCalendarId(string calendarId)
{
    _data["cid"] = calendarId;
}

string Event::href()
{
    return _data.count("href") ? _data["href"].get<string>() : "";
}

void Event::setHref(string href)
{
    _data["href"] = href;
}

string Event::icsData()
{
    return _data["ics"].get<string>();
}

void Event::setIcsData(string ics)
{
    _data["ics"] = ics;
}

string Event::icsUID()
{
    return _data["icsuid"].get<string>();
}

string Event::etag()
{
    return _data["etag"].get<string>();
}

void Event::setEtag(string etag)
{
    _data["etag"] = etag;
}

string Event::recurrenceId()
{
    return _data.count("rid") ? _data["rid"].get<string>() : "";
}

void Event::setRecurrenceId(string recurrenceId)
{
    _data["rid"] = recurrenceId;
}

string Event::status()
{
    return _data.count("status") ? _data["status"].get<string>() : "CONFIRMED";
}

void Event::setStatus(string status)
{
    _data["status"] = status;
}

bool Event::isRecurrenceException()
{
    return !recurrenceId().empty();
}

int Event::recurrenceStart()
{
    return _data["rs"].get<int>();
}

int Event::recurrenceEnd()
{
    return _data["re"].get<int>();
}

vector<string> Event::columnsForQuery()
{
    return vector<string>{"id", "data", "icsuid", "recurrenceId", "accountId", "etag", "calendarId", "recurrenceStart", "recurrenceEnd"};
}

void Event::bindToQuery(SQLite::Statement *query)
{
    query->bind(":id", id());
    query->bind(":data", this->toJSON().dump());
    query->bind(":icsuid", icsUID());
    query->bind(":recurrenceId", recurrenceId());
    query->bind(":accountId", accountId());
    query->bind(":etag", etag());
    query->bind(":calendarId", calendarId());
    query->bind(":recurrenceStart", recurrenceStart());
    query->bind(":recurrenceEnd", recurrenceEnd());
}
