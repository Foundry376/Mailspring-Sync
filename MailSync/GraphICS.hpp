//
//  GraphICS.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef GraphICS_hpp
#define GraphICS_hpp

#include <ctime>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"

using namespace nlohmann;
using namespace std;

// One content line of an iCalendar component, unfolded (RFC 5545 section 3.1). The name and
// parameter names are upper case, parameter values unquoted, and the value as written.
struct ICSProperty {
    string name;
    map<string, string> params;
    string value;

    string param(const string & name) const;
    // An ATTENDEE or ORGANIZER value's address, without "mailto:".
    string calAddress() const;
};

// The properties of one iCalendar component, in order.
struct ICSComponent {
    vector<ICSProperty> properties;

    // The first property with this name, or an empty one.
    const ICSProperty & property(const string & name) const;
};

// A VEVENT's own properties, and each VALARM inside it apart from them.
struct ICSVEvent : ICSComponent {
    vector<ICSComponent> alarms;
};

// An edit Graph has no way to make. `key` is the task error the client looks up.
struct GraphEditRefused : runtime_error {
    string key;
    GraphEditRefused(const string & key, const string & message) : runtime_error(message), key(key) {}
};

// One Graph write of an edit, in the order they must be made.
struct GraphEventChange {
    enum class Kind { Create, Update, UpdateOccurrence, CancelOccurrence };
    Kind kind;
    // UpdateOccurrence: its RECURRENCE-ID. CancelOccurrence: one EXDATE value, with its parameters.
    ICSProperty occurrence;
    // Create, Update, UpdateOccurrence: the Graph fields to send.
    json fields;
    // UpdateOccurrence: written again only because the series' times or recurrence changed, not
    // because the edit touched the occurrence.
    bool reapplied = false;
};

// Microsoft Graph events as the iCalendar resources Event rows hold; Graph has no iCalendar form.
class GraphICS {
public:
    // `event` fetched in its own zone (Prefer: outlook.timezone) with a text body; a series master
    // also with $select=cancelledOccurrences and $expand=exceptionOccurrences. `mailboxAddresses`:
    // the user's own, whose attendee entry gets the mailbox's answer. `startUTC`: the event's start
    // as Graph lists it in UTC, which gives a series its UTC offset.
    static string icsForEvent(const json & event, const vector<string> & mailboxAddresses, const string & startUTC);

    // The VEVENTs of a resource the client wrote. icalendarlib reads no BYDAY, EXDATE, TRANSP or
    // attendee parameters, and splits a line at its first ':' even inside a quoted parameter.
    static vector<ICSVEvent> veventsOf(const string & ics);

    // The Graph fields Mailspring edits, from one VEVENT: subject, body, location, start, end,
    // isAllDay, showAs, attendees, the reminder (isReminderOn, and reminderMinutesBeforeStart while
    // it's on) and, unless it has a RECURRENCE-ID, recurrence (null without an RRULE). `startUTC` as
    // for icsForEvent, or "" for an event Graph doesn't hold yet. Refuses an RRULE that
    // patternedRecurrence can't express.
    static json eventForVEvent(const ICSVEvent & vevent, const string & startUTC);

    // The Graph writes that turn the resource `before` (the row as the user saw it, "" for a new
    // event) into `after` (the client's edit). Only fields the edit changed are sent, so what
    // iCalendar can't carry (an HTML body, showAs tentative, categories) and a reminder the edit
    // left alone survive. A change to the series' times or recurrence re-applies every override and
    // EXDATE, which the client moved with it.
    // `startUTC`: the start of `before` as Graph lists it in UTC.
    static vector<GraphEventChange> changesBetween(const string & before, const string & after, const string & startUTC);

    // Whether Graph's `originalStart` is the occurrence `anchor` (a RECURRENCE-ID, or one EXDATE
    // value) names. `originalStart` read in the anchor's zone: its TZID, or for a date the series'.
    static bool namesOccurrence(const ICSProperty & anchor, const string & originalStart);

    // A UTC range (startDateTime, endDateTime for /instances) holding the occurrence `anchor` names.
    static pair<string, string> occurrenceWindow(const ICSProperty & anchor);

    // A Graph date-time or date ("2026-10-22T09:00:00.0000000", "2026-12-31") read as UTC.
    static time_t secondsAsUTC(const string & extended);
};

#endif /* GraphICS_hpp */
