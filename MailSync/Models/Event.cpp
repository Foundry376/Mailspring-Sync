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
    _data["icsuid"] = event->UID;
    applyICSEventData(etag, "", ics, event);
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

void Event::applyICSEventData(const string& etag, const string& href,
                              const string& icsData, ICalendarEvent* icsEvent)
{
    setEtag(etag);
    if (!href.empty()) {
        setHref(href);
    }
    setIcsData(icsData);
    setRecurrenceId(icsEvent->RecurrenceId);
    setStatus(icsEvent->Status.empty() ? "CONFIRMED" : icsEvent->Status);
    _data["rs"] = icsEvent->DtStart.toUnix();
    _data["re"] = endOf(icsEvent).toUnix();

    // Populate transient search fields from the ICalendarEvent
    _searchTitle = icsEvent->Summary;
    _searchDescription = icsEvent->Description;
    _searchLocation = icsEvent->Location;

    // Join attendees list into a single searchable string
    _searchParticipants = "";
    for (const auto& attendee : icsEvent->Attendees) {
        if (!_searchParticipants.empty()) {
            _searchParticipants += " ";
        }
        _searchParticipants += attendee;
    }
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

void Event::afterSave(MailStore * store) {
    MailModel::afterSave(store);

    // Only update EventSearch if search content was populated via applyICSEventData.
    // Events loaded from DB or client JSON won't have search content set.
    if (_searchTitle.empty() && _searchDescription.empty() &&
        _searchLocation.empty() && _searchParticipants.empty()) {
        return;
    }

    if (version() == 1) {
        SQLite::Statement insert(store->db(), "INSERT INTO EventSearch (content_id, title, description, location, participants) VALUES (?, ?, ?, ?, ?)");
        insert.bind(1, id());
        insert.bind(2, _searchTitle);
        insert.bind(3, _searchDescription);
        insert.bind(4, _searchLocation);
        insert.bind(5, _searchParticipants);
        insert.exec();
    } else {
        SQLite::Statement update(store->db(), "UPDATE EventSearch SET title = ?, description = ?, location = ?, participants = ? WHERE content_id = ?");
        update.bind(1, _searchTitle);
        update.bind(2, _searchDescription);
        update.bind(3, _searchLocation);
        update.bind(4, _searchParticipants);
        update.bind(5, id());
        update.exec();
    }
}

void Event::afterRemove(MailStore * store) {
    MailModel::afterRemove(store);

    SQLite::Statement remove(store->db(), "DELETE FROM EventSearch WHERE content_id = ?");
    remove.bind(1, id());
    remove.exec();
}
