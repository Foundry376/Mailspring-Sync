//
//  graph_ics_test.cpp
//  Unit tests for turning Microsoft Graph events into iCalendar resources and back. Each case
//  also parses GraphICS's output with icalendarlib, the engine's own reader of Event.ics.
//
//  Build and run (Linux): cmake . && make graph_ics_test && ./graph_ics_test
//

#include "GraphICS.hpp"
#include "icalendar.h"
#include "strlib.h"

#include <iostream>

static int failures = 0;

static void expect(bool ok, const string & label, const string & detail = "") {
    if (!ok) {
        cerr << "FAIL " << label << (detail.empty() ? "" : ": " + detail) << "\n";
        failures++;
    }
}

static void expectEqual(const string & actual, const string & expected, const string & label) {
    expect(actual == expected, label, "expected \"" + expected + "\", got \"" + actual + "\"");
}

static void expectLine(const string & ics, const string & line, const string & label) {
    expect(("\r\n" + ics).find("\r\n" + line + "\r\n") != string::npos, label,
           "no line \"" + line + "\" in\n" + ics);
}

static json event(json fields) {
    json e = {
        {"iCalUId", "040000008200E00074C5B7101A82E0080000000052A4C9D3F05CDD01000000000000000010000000A3B19F1C27E8D24E9C5A3B7F0D1E2C4B"},
        {"subject", "Planning"},
        {"lastModifiedDateTime", "2026-10-20T17:50:34.1234567Z"},
        {"isAllDay", false},
        {"isCancelled", false},
        {"showAs", "busy"},
        {"start", {{"dateTime", "2026-10-22T09:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
        {"end", {{"dateTime", "2026-10-22T09:30:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
    };
    if (fields.is_object()) { // `{}` arrives as null
        e.update(fields);
    }
    return e;
}

static const vector<string> ME = {"me@example.test"};
// 09:00 on 2026-10-22 in Pacific daylight time, as Graph lists the event in UTC.
static const string START_UTC = "2026-10-22T16:00:00.0000000";

// A series master starting Monday 2026-10-05 at 09:00 Pacific daylight time (16:00 UTC).
static json series(json recurrence, json fields = json::object()) {
    json master = event({
        {"type", "seriesMaster"},
        {"start", {{"dateTime", "2026-10-05T09:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
        {"end", {{"dateTime", "2026-10-05T09:30:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
        {"recurrence", recurrence},
    });
    master.update(fields);
    return master;
}

static json pattern(json fields, json range = {{"type", "noEnd"}, {"startDate", "2026-10-05"}}) {
    return {{"pattern", fields}, {"range", range}};
}

static const string SERIES_START_UTC = "2026-10-05T16:00:00.0000000";

static string rruleOf(const json & master, const string & startUTC = SERIES_START_UTC) {
    string ics = GraphICS::icsForEvent(master, ME, startUTC);
    size_t at = ics.find("\r\nRRULE:");
    return at == string::npos ? "" : ics.substr(at + 2, ics.find("\r\n", at + 2) - at - 2);
}

static void expectJSON(const json & actual, const json & expected, const string & label) {
    expect(actual == expected, label, "expected " + expected.dump() + ", got " + actual.dump());
}

// A resource holding one VEVENT with these lines, as the client sends it.
static string vcalendar(const string & veventLines) {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:abc@mailspring\r\n" + veventLines + "END:VEVENT\r\nEND:VCALENDAR\r\n";
}

static json graphEvent(const string & ics, const string & startUTC = "") {
    vector<ICSVEvent> vevents = GraphICS::veventsOf(ics);
    return vevents.empty() ? json() : GraphICS::eventForVEvent(vevents.front(), startUTC);
}

// A Wednesday-morning series in Chicago with this RRULE, as the client's Repeat control writes it.
static json recurrenceFor(const string & rrule) {
    return graphEvent(vcalendar("DTSTART;TZID=America/Chicago:20261007T090000\r\n"
                                "DTEND;TZID=America/Chicago:20261007T093000\r\n"
                                "RRULE:" + rrule + "\r\n"))["recurrence"];
}

static bool refused(const string & rrule) {
    try {
        recurrenceFor(rrule);
    } catch (GraphEditRefused &) {
        return true;
    }
    return false;
}

// GraphICS's series written out and read back: the pattern and range it started from.
static void expectRoundTrip(const json & master, const string & label, const string & startUTC = SERIES_START_UTC) {
    vector<ICSVEvent> vevents = GraphICS::veventsOf(GraphICS::icsForEvent(master, ME, startUTC));
    if (vevents.empty()) {
        expect(false, label, "no VEVENT");
        return;
    }
    json recurrence = GraphICS::eventForVEvent(vevents.front(), startUTC)["recurrence"];
    for (const auto & [key, value] : master["recurrence"]["pattern"].items()) {
        expectJSON(recurrence["pattern"][key], value, label + ": pattern " + key);
    }
    for (const string key : {"type", "startDate", "endDate", "numberOfOccurrences"}) {
        if (master["recurrence"]["range"].count(key)) {
            expectJSON(recurrence["range"][key], master["recurrence"]["range"][key], label + ": range " + key);
        }
    }
}

// `text` with its one `from` replaced, as the client's edit would leave it.
static string edited(const string & text, const string & from, const string & to) {
    size_t at = text.find(from);
    expect(at != string::npos && text.find(from, at + 1) == string::npos, "fixture edit", "\"" + from + "\" not found once");
    return at == string::npos ? text : text.substr(0, at) + to + text.substr(at + from.size());
}

// The writes, one line each: "update subject", "occurrence 20261019T160000Z start,end",
// "cancel Pacific Standard Time:20261109T090000".
static string writesFor(const string & before, const string & after) {
    static const map<GraphEventChange::Kind, string> NAMES = {
        {GraphEventChange::Kind::Create, "create"}, {GraphEventChange::Kind::Update, "update"},
        {GraphEventChange::Kind::UpdateOccurrence, "occurrence"}, {GraphEventChange::Kind::CancelOccurrence, "cancel"},
    };
    vector<string> lines;
    for (const auto & change : GraphICS::changesBetween(before, after, SERIES_START_UTC)) {
        vector<string> fields;
        if (change.fields.is_object()) {
            for (const auto & [name, value] : change.fields.items()) {
                fields.push_back(name);
            }
        }
        string occurrence = change.occurrence.param("TZID") + (change.occurrence.param("TZID") == "" ? "" : ":") + change.occurrence.value;
        lines.push_back(NAMES.at(change.kind) + (occurrence == "" ? "" : " " + occurrence) + (fields.empty() ? "" : " " + stringJoin(fields, ",")));
    }
    return stringJoin(lines, "\n");
}

static string refusalFor(const string & before, const string & after) {
    try {
        GraphICS::changesBetween(before, after, SERIES_START_UTC);
    } catch (GraphEditRefused & ex) {
        return ex.key;
    }
    return "";
}

int main() {
    {
        // Fetched in its own zone, a timed event keeps Graph's Windows zone name as its TZID.
        string ics = GraphICS::icsForEvent(event({}), ME, START_UTC);
        expectLine(ics, "DTSTART;TZID=Pacific Standard Time:20261022T090000", "zoned DTSTART");
        expectLine(ics, "DTEND;TZID=Pacific Standard Time:20261022T093000", "zoned DTEND");
        expectLine(ics, "DTSTAMP:20261020T175034Z", "DTSTAMP from lastModifiedDateTime");
        expectLine(ics, "STATUS:CONFIRMED", "confirmed");
        expectLine(ics, "TRANSP:OPAQUE", "busy is opaque");
        ICalendar cal(ics);
        expect(cal.Events.size() == 1, "one VEVENT");
        if (!cal.Events.empty()) {
            ICalendarEvent * e = cal.Events.front();
            expectEqual(e->UID, "040000008200E00074C5B7101A82E0080000000052A4C9D3F05CDD01000000000000000010000000A3B19F1C27E8D24E9C5A3B7F0D1E2C4B", "parsed UID");
            expectEqual(string(e->DtStart), "20261022T090000", "parsed DTSTART");
            expectEqual(e->Summary, "Planning", "parsed SUMMARY");
            expect(e->RecurrenceId.empty(), "no RECURRENCE-ID");
        }
    }
    {
        // A time in UTC is written with Z, the only form the engine turns into an exact instant.
        string ics = GraphICS::icsForEvent(event({
            {"start", {{"dateTime", "2026-10-22T16:00:00.0000000"}, {"timeZone", "UTC"}}},
            {"end", {{"dateTime", "2026-10-22T17:00:00.0000000"}, {"timeZone", "UTC"}}},
        }), ME, START_UTC);
        expectLine(ics, "DTSTART:20261022T160000Z", "UTC DTSTART");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expect(cal.Events.front()->DtStart.toUnix() == 1792684800, "UTC start instant",
                   to_string(cal.Events.front()->DtStart.toUnix()));
            expect(cal.Events.front()->DtEnd.toUnix() == 1792688400, "UTC end instant");
        }
    }
    {
        // All-day events are DATE values; Graph's end is already the exclusive next midnight.
        string oneDay = GraphICS::icsForEvent(event({
            {"isAllDay", true},
            {"start", {{"dateTime", "2026-10-22T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
            {"end", {{"dateTime", "2026-10-23T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
        }), ME, START_UTC);
        expectLine(oneDay, "DTSTART;VALUE=DATE:20261022", "all-day DTSTART");
        expectLine(oneDay, "DTEND;VALUE=DATE:20261023", "all-day DTEND");
        ICalendar cal(oneDay);
        if (!cal.Events.empty()) {
            expect(!cal.Events.front()->DtStart.WithTime, "parsed as a date");
        }

        string threeDays = GraphICS::icsForEvent(event({
            {"isAllDay", true},
            {"start", {{"dateTime", "2026-10-22T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
            {"end", {{"dateTime", "2026-10-25T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
        }), ME, START_UTC);
        expectLine(threeDays, "DTEND;VALUE=DATE:20261025", "multi-day DTEND");
    }
    {
        // Every response maps to its PARTSTAT. The mailbox's own entry still says "none" while
        // responseStatus already holds its decline.
        string ics = GraphICS::icsForEvent(event({
            {"organizer", {{"emailAddress", {{"name", "Pat Lee"}, {"address", "pat@example.com"}}}}},
            {"responseStatus", {{"response", "declined"}, {"time", "2026-10-21T08:00:00Z"}}},
            {"attendees", json::array({
                {{"type", "required"}, {"status", {{"response", "accepted"}}},
                 {"emailAddress", {{"name", "Doe, Jane"}, {"address", "jane@example.com"}}}},
                {{"type", "optional"}, {"status", {{"response", "tentativelyAccepted"}}},
                 {"emailAddress", {{"name", "Sam"}, {"address", "sam@example.com"}}}},
                {{"type", "required"}, {"status", {{"response", "notResponded"}}},
                 {"emailAddress", {{"name", ""}, {"address", "kim@example.com"}}}},
                {{"type", "resource"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Room 4"}, {"address", "room4@example.com"}}}},
                {{"type", "required"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Me"}, {"address", "ME@example.test"}}}},
                {{"type", "required"}, {"status", {{"response", "organizer"}}},
                 {"emailAddress", {{"name", "Pat Lee"}, {"address", "pat@example.com"}}}},
            })},
        }), ME, START_UTC);
        expectLine(ics, "ORGANIZER;CN=Pat Lee:mailto:pat@example.com", "organizer");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;CN=\"Doe, Jane\":mailto:jane@example.com",
                   "accepted, name with a comma quoted");
        expectLine(ics, "ATTENDEE;ROLE=OPT-PARTICIPANT;PARTSTAT=TENTATIVE;CN=Sam:mailto:sam@example.com", "tentative");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION:mailto:kim@example.com", "unanswered, no name");
        expectLine(ics, "ATTENDEE;CUTYPE=RESOURCE;ROLE=NON-PARTICIPANT;PARTSTAT=NEEDS-ACTION;CN=Room 4:mailto:room4@example.com",
                   "resource");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=DECLINED;CN=Me:mailto:ME@example.test",
                   "own answer from responseStatus");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;CN=Pat Lee:mailto:pat@example.com",
                   "organizer listed among attendees");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expect(cal.Events.front()->Attendees.size() == 6, "parsed attendees",
                   to_string(cal.Events.front()->Attendees.size()));
        }
    }
    {
        // Signed in with an alias, the user is invited at the primary address: either one is the
        // mailbox's own entry.
        string ics = GraphICS::icsForEvent(event({
            {"responseStatus", {{"response", "tentativelyAccepted"}}},
            {"attendees", json::array({
                {{"type", "required"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Me"}, {"address", "me@primary.example.test"}}}},
                {{"type", "optional"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Me"}, {"address", "me@alias.example.test"}}}},
            })},
        }), {"me@alias.example.test", "me@primary.example.test"}, START_UTC);
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=TENTATIVE;CN=Me:mailto:me@primary.example.test",
                   "own answer at the primary address");
        expectLine(ics, "ATTENDEE;ROLE=OPT-PARTICIPANT;PARTSTAT=TENTATIVE;CN=Me:mailto:me@alias.example.test",
                   "own answer at the alias");
    }
    {
        // A meeting a Google user organized: Exchange's ID wraps the organizer's UID, which the
        // invitation carries ([MS-OXCICAL] 2.1.3.1.1.20.26), with or without a trailing NUL.
        expectLine(GraphICS::icsForEvent(event({{"iCalUId", "040000008200E00074C5B7101A82E00800000000000000000000000000000000000000002B0000007643616C2D556964010000003676713065316C726E6A35673863346D3361326240676F6F676C652E636F6D"}}), ME, START_UTC),
                   "UID:6vq0e1lrnj5g8c4m3a2b@google.com", "organizer's UID unwrapped");
        expectLine(GraphICS::icsForEvent(event({{"iCalUId", "040000008200E00074C5B7101A82E00800000000000000000000000000000000000000002C0000007643616C2D556964010000003676713065316C726E6A35673863346D3361326240676F6F676C652E636F6D00"}}), ME, START_UTC),
                   "UID:6vq0e1lrnj5g8c4m3a2b@google.com", "organizer's UID unwrapped, trailing NUL dropped");
        // An orphan occurrence (16 November 2026) keeps the whole ID, which differs per occurrence.
        expectLine(GraphICS::icsForEvent(event({{"iCalUId", "040000008200E00074C5B7101A82E00807EA0B10000000000000000000000000000000002C0000007643616C2D556964010000003676713065316C726E6A35673863346D3361326240676F6F676C652E636F6D00"}}), ME, START_UTC),
                   "UID:040000008200E00074C5B7101A82E00807EA0B10000000000000000000000000000000002C0000007643616C2D556964010000003676713065316C726E6A35673863346D3361326240676F6F676C652E636F6D00", "orphan occurrence keeps its own ID");
    }
    {
        string ics = GraphICS::icsForEvent(event({{"isCancelled", true}, {"showAs", "free"}}), ME, START_UTC);
        expectLine(ics, "STATUS:CANCELLED", "cancelled");
        expectLine(ics, "TRANSP:TRANSPARENT", "free is transparent");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expectEqual(cal.Events.front()->Status, "CANCELLED", "parsed STATUS");
        }
    }
    {
        // Text is escaped and stays on one line however long.
        string notes;
        for (int i = 0; i < 40; i++) {
            notes += "Café ☕ ";
        }
        string ics = GraphICS::icsForEvent(event({
            {"subject", "Budget, Q4; draft \\ v2"},
            {"location", {{"displayName", "Room 4, Building 2"}}},
            {"body", {{"contentType", "text"}, {"content", "Agenda:\r\n1. Numbers\r\n" + notes}}},
        }), ME, START_UTC);
        expectLine(ics, "SUMMARY:Budget\\, Q4\\; draft \\\\ v2", "SUMMARY escaped");
        expectLine(ics, "DESCRIPTION:Agenda:\\n1. Numbers\\n" + notes, "DESCRIPTION escaped, on one line");
        expectLine(ics, "LOCATION:Room 4\\, Building 2", "LOCATION escaped");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expectEqual(cal.Events.front()->Description, "Agenda:\\n1. Numbers\\n" + notes, "parsed DESCRIPTION intact");
            expectEqual(cal.Events.front()->Location, "Room 4, Building 2", "parsed LOCATION unescaped");
        }
    }
    {
        // Each Graph pattern as the RRULE Google would write for it.
        expectEqual(rruleOf(series(pattern({{"type", "daily"}, {"interval", 3}}))),
                    "RRULE:FREQ=DAILY;INTERVAL=3", "daily every 3 days");
        expectEqual(rruleOf(series(pattern({{"type", "relativeMonthly"}, {"interval", 1},
                                            {"daysOfWeek", {"friday"}}, {"index", "last"}},
                                           {{"type", "numbered"}, {"numberOfOccurrences", 10}}))),
                    "RRULE:FREQ=MONTHLY;BYDAY=-1FR;COUNT=10", "last Friday, ten times");
        expectEqual(rruleOf(series(pattern({{"type", "relativeMonthly"}, {"interval", 1}, {"index", "first"},
                                            {"daysOfWeek", {"monday", "tuesday", "wednesday", "thursday", "friday"}}}))),
                    "RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=1", "first weekday of the month");
        expectEqual(rruleOf(series(pattern({{"type", "absoluteMonthly"}, {"interval", 3}, {"dayOfMonth", 15}}))),
                    "RRULE:FREQ=MONTHLY;INTERVAL=3;BYMONTHDAY=15", "every third month on the 15th");
        expectEqual(rruleOf(series(pattern({{"type", "absoluteYearly"}, {"interval", 1}, {"month", 3},
                                            {"dayOfMonth", 14}}))),
                    "RRULE:FREQ=YEARLY;BYMONTH=3;BYMONTHDAY=14", "yearly on March 14");
        expectEqual(rruleOf(series(pattern({{"type", "absoluteMonthly"}, {"interval", 1}, {"dayOfMonth", 31}}))),
                    "RRULE:FREQ=MONTHLY;BYMONTHDAY=-1", "the 31st, or the last day of a shorter month");
        expectEqual(rruleOf(series(pattern({{"type", "absoluteYearly"}, {"interval", 1}, {"month", 2},
                                            {"dayOfMonth", 29}}))),
                    "RRULE:FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=-1", "February 29, or the 28th");
        expectEqual(rruleOf(series(pattern({{"type", "relativeYearly"}, {"interval", 1}, {"month", 11},
                                            {"daysOfWeek", {"tuesday"}}, {"index", "second"}}))),
                    "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=2TU", "second Tuesday of November");
    }
    {
        // Starts in daylight time (UTC-7), ends in standard (UTC-8): the last occurrence, Wednesday
        // 2026-12-16 09:00, is 17:00 UTC, which an UNTIL at the start's offset (16:00) would drop.
        json master = series(pattern({{"type", "weekly"}, {"interval", 2}, {"daysOfWeek", {"monday", "wednesday"}},
                                      {"firstDayOfWeek", "sunday"}},
                                     {{"type", "endDate"}, {"startDate", "2026-10-05"}, {"endDate", "2026-12-16"},
                                      {"recurrenceTimeZone", "Pacific Standard Time"}}));
        expectEqual(rruleOf(master), "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;WKST=SU;UNTIL=20261216T180000Z",
                    "biweekly until a date across daylight saving");
        ICalendar cal(GraphICS::icsForEvent(master, ME, SERIES_START_UTC));
        if (!cal.Events.empty()) {
            int until = cal.Events.front()->RRule.Until.toUnix();
            expect(until >= 1797440400, "UNTIL not before the last occurrence", to_string(until));
            expect(!cal.Events.front()->RRule.IsEmpty(), "parsed RRULE");
        }
    }
    {
        json master = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                      {"firstDayOfWeek", "sunday"}},
                                     {{"type", "endDate"}, {"startDate", "2026-10-05"}, {"endDate", "2026-12-28"}}),
                             {{"isAllDay", true},
                              {"start", {{"dateTime", "2026-10-05T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              {"end", {{"dateTime", "2026-10-06T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}}});
        expectEqual(rruleOf(master, "2026-10-05T07:00:00.0000000"),
                    "RRULE:FREQ=WEEKLY;BYDAY=MO;WKST=SU;UNTIL=20261228", "all-day UNTIL is a date");
    }
    {
        json master = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                      {"firstDayOfWeek", "sunday"}}),
                             {{"cancelledOccurrences", {"OID.AAMkMaster=.2026-11-02"}},
                              {"exceptionOccurrences", json::array({event({
                                  {"type", "exception"},
                                  {"iCalUId", "040000008200E00074C5B7101A82E00807EA0A130000000052A4C9D3F05CDD01000000000000000010000000A3B19F1C27E8D24E9C5A3B7F0D1E2C4B"},
                                  {"subject", "Planning (moved)"},
                                  {"originalStart", "2026-10-19T16:00:00Z"},
                                  {"start", {{"dateTime", "2026-10-19T10:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                                  {"end", {{"dateTime", "2026-10-19T10:30:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              })})}});
        string ics = GraphICS::icsForEvent(master, ME, SERIES_START_UTC);
        expectLine(ics, "EXDATE;TZID=Pacific Standard Time:20261102T090000", "cancelled date");
        expectLine(ics, "RECURRENCE-ID:20261019T160000Z", "override keyed by original start");
        expectLine(ics, "DTSTART;TZID=Pacific Standard Time:20261019T100000", "override's own start");
        expectLine(ics, "SUMMARY:Planning (moved)", "override's own subject");
        ICalendar cal(ics);
        expect(cal.Events.size() == 2, "master and override", to_string(cal.Events.size()));
        if (cal.Events.size() == 2) {
            expect(cal.Events.front()->RecurrenceId.empty(), "master has no RECURRENCE-ID");
            expectEqual(cal.Events.back()->UID, cal.Events.front()->UID, "override shares the master's UID");
            expectEqual(cal.Events.back()->RecurrenceId, "20261019T160000Z", "parsed RECURRENCE-ID");
        }
    }
    {
        // An all-day series that starts in standard time (UTC-8): an occurrence in daylight time
        // starts at 07:00 UTC, which the series' offset alone puts at 23:00 the day before.
        json master = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                      {"firstDayOfWeek", "sunday"}}),
                             {{"isAllDay", true},
                              {"start", {{"dateTime", "2027-01-04T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              {"end", {{"dateTime", "2027-01-05T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              {"cancelledOccurrences", {"OID.AAMkMaster=.2027-04-12"}},
                              {"exceptionOccurrences", json::array({event({
                                  {"type", "exception"},
                                  {"isAllDay", true},
                                  {"subject", "Offsite"},
                                  {"originalStart", "2027-04-05T07:00:00Z"},
                                  {"start", {{"dateTime", "2027-04-05T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                                  {"end", {{"dateTime", "2027-04-06T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              })})}});
        string ics = GraphICS::icsForEvent(master, ME, "2027-01-04T08:00:00.0000000");
        expectLine(ics, "RECURRENCE-ID;VALUE=DATE:20270405", "all-day override on its own date across daylight saving");
        expectLine(ics, "EXDATE;VALUE=DATE:20270412", "all-day cancelled date");
    }
    {
        // East of UTC an all-day occurrence's local midnight falls on the previous UTC day. Tokyo
        // (UTC+9) is a fixed offset; Auckland moves from UTC+12 to UTC+13 after the series starts.
        json tokyo = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                     {"firstDayOfWeek", "sunday"}}),
                            {{"isAllDay", true},
                             {"start", {{"dateTime", "2027-01-04T00:00:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}},
                             {"end", {{"dateTime", "2027-01-05T00:00:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}},
                             {"exceptionOccurrences", json::array({event({
                                 {"type", "exception"},
                                 {"isAllDay", true},
                                 {"originalStart", "2027-04-04T15:00:00Z"},
                                 {"start", {{"dateTime", "2027-04-05T00:00:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}},
                                 {"end", {{"dateTime", "2027-04-06T00:00:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}},
                             })})}});
        expectLine(GraphICS::icsForEvent(tokyo, ME, "2027-01-03T15:00:00.0000000"),
                   "RECURRENCE-ID;VALUE=DATE:20270405", "all-day override east of UTC");

        json auckland = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                        {"firstDayOfWeek", "sunday"}}),
                               {{"isAllDay", true},
                                {"start", {{"dateTime", "2026-07-06T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                {"end", {{"dateTime", "2026-07-07T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                {"exceptionOccurrences", json::array({event({
                                    {"type", "exception"},
                                    {"isAllDay", true},
                                    {"originalStart", "2027-01-31T11:00:00Z"},
                                    {"start", {{"dateTime", "2027-02-01T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                    {"end", {{"dateTime", "2027-02-02T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                })})}});
        expectLine(GraphICS::icsForEvent(auckland, ME, "2026-07-05T12:00:00.0000000"),
                   "RECURRENCE-ID;VALUE=DATE:20270201", "all-day override east of UTC across daylight saving");
    }
    {
        // Graph writes originalStart in the zone it was asked for, with that zone's offset, as an
        // Outlook.com account does: 12:00 Central on 2 November, after daylight saving ends, is 18:00 UTC.
        json master = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                      {"firstDayOfWeek", "sunday"}}, {{"type", "noEnd"}, {"startDate", "2026-10-19"}}),
                             {{"start", {{"dateTime", "2026-10-19T12:00:00.0000000"}, {"timeZone", "Central Standard Time"}}},
                              {"end", {{"dateTime", "2026-10-19T12:30:00.0000000"}, {"timeZone", "Central Standard Time"}}},
                              {"exceptionOccurrences", json::array({event({
                                  {"type", "exception"},
                                  {"originalStart", "2026-11-02T12:00:00-06:00"},
                                  {"start", {{"dateTime", "2026-11-02T13:00:00.0000000"}, {"timeZone", "Central Standard Time"}}},
                                  {"end", {{"dateTime", "2026-11-02T13:30:00.0000000"}, {"timeZone", "Central Standard Time"}}},
                              })})}});
        expectLine(GraphICS::icsForEvent(master, ME, "2026-10-19T17:00:00.0000000"),
                   "RECURRENCE-ID:20261102T180000Z", "override keyed by an original start written with its offset");

        json auckland = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}},
                                        {"firstDayOfWeek", "sunday"}}),
                               {{"isAllDay", true},
                                {"start", {{"dateTime", "2026-07-06T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                {"end", {{"dateTime", "2026-07-07T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                {"exceptionOccurrences", json::array({event({
                                    {"type", "exception"},
                                    {"isAllDay", true},
                                    {"originalStart", "2027-02-01T00:00:00+13:00"},
                                    {"start", {{"dateTime", "2027-02-01T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                    {"end", {{"dateTime", "2027-02-02T00:00:00.0000000"}, {"timeZone", "New Zealand Standard Time"}}},
                                })})}});
        expectLine(GraphICS::icsForEvent(auckland, ME, "2026-07-05T12:00:00.0000000"),
                   "RECURRENCE-ID;VALUE=DATE:20270201", "all-day override whose original start carries its offset");
    }

    // iCalendar the client wrote, as Graph event fields.
    {
        // A new event as ical.js writes it: an IANA zone with its VTIMEZONE, a folded line, quoted
        // parameter values holding ':' and ';', and an alarm whose DESCRIPTION isn't the event's.
        json e = graphEvent(
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VTIMEZONE\r\nTZID:America/Chicago\r\nBEGIN:STANDARD\r\nTZOFFSETFROM:-0500\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"
            "BEGIN:VEVENT\r\nUID:abc@mailspring\r\n"
            "DTSTART;TZID=America/Chicago:20261022T090000\r\nDTEND;TZID=America/Chicago:20261022T093000\r\n"
            "SUMMARY:Budget\\, Q4\\; draft \\\\ v2\r\n"
            "DESCRIPTION:Agenda:\\n1. Num\r\n bers\r\n"
            "LOCATION:Room 4\\, Building 2\r\n"
            "ATTENDEE;CN=\"Team: Design; Ops\";ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION:mailto:team@example.com\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n");
        expectJSON(e["subject"], "Budget, Q4; draft \\ v2", "subject unescaped");
        expectJSON(e["body"], {{"contentType", "text"}, {"content", "Agenda:\n1. Numbers"}}, "body unfolded, not the alarm's");
        expectJSON(e["location"], {{"displayName", "Room 4, Building 2"}}, "location unescaped");
        expectJSON(e["start"], {{"dateTime", "2026-10-22T09:00:00"}, {"timeZone", "America/Chicago"}}, "IANA start");
        expectJSON(e["end"], {{"dateTime", "2026-10-22T09:30:00"}, {"timeZone", "America/Chicago"}}, "IANA end");
        expectJSON(e["isAllDay"], false, "timed");
        expectJSON(e["showAs"], "busy", "no TRANSP is busy");
        expectJSON(e["attendees"], json::array({{{"type", "required"},
                   {"emailAddress", {{"address", "team@example.com"}, {"name", "Team: Design; Ops"}}}}}),
                   "quoted name holding ':' and ';'");
        expectJSON(e["recurrence"], json(), "no RRULE");
        vector<ICSVEvent> vevents = GraphICS::veventsOf(vcalendar("BEGIN:VALARM\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"));
        expect(!vevents.empty() && vevents.front().property("TRIGGER").name.empty(), "an alarm's properties aren't the event's");
    }
    {
        json utc = graphEvent(vcalendar("DTSTART:20261022T160000Z\r\nDTEND:20261022T170000Z\r\nTRANSP:TRANSPARENT\r\n"));
        expectJSON(utc["start"], {{"dateTime", "2026-10-22T16:00:00"}, {"timeZone", "UTC"}}, "UTC start");
        expectJSON(utc["showAs"], "free", "transparent is free");

        json custom = graphEvent(vcalendar("DTSTART;TZID=\"tzone://Microsoft/Custom\":20261022T090000\r\n"
                                           "DTEND;TZID=\"tzone://Microsoft/Custom\":20261022T100000\r\n"));
        expectJSON(custom["start"]["timeZone"], "tzone://Microsoft/Custom", "quoted TZID holding ':'");

        json allDay = graphEvent(vcalendar("DTSTART;VALUE=DATE:20261022\r\nDTEND;VALUE=DATE:20261025\r\n"));
        expectJSON(allDay["isAllDay"], true, "all-day");
        expectJSON(allDay["start"], {{"dateTime", "2026-10-22T00:00:00"}, {"timeZone", "UTC"}}, "all-day start at midnight");
        expectJSON(allDay["end"], {{"dateTime", "2026-10-25T00:00:00"}, {"timeZone", "UTC"}}, "all-day end at the next midnight");
    }
    {
        // The attendees GraphICS writes, read back as the Graph attendees they came from.
        json original = event({
            {"attendees", json::array({
                {{"type", "required"}, {"status", {{"response", "accepted"}}},
                 {"emailAddress", {{"name", "Doe, Jane"}, {"address", "jane@example.com"}}}},
                {{"type", "optional"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Sam"}, {"address", "sam@example.com"}}}},
                {{"type", "resource"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Room 4"}, {"address", "room4@example.com"}}}},
                {{"type", "required"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", ""}, {"address", "kim@example.com"}}}},
            })},
        });
        json e = graphEvent(GraphICS::icsForEvent(original, ME, START_UTC), START_UTC);
        json expected = json::array();
        for (const auto & attendee : original["attendees"]) {
            expected.push_back({{"type", attendee["type"]}, {"emailAddress", attendee["emailAddress"]}});
        }
        expectJSON(e["attendees"], expected, "attendees round trip");
        expectJSON(e["start"], {{"dateTime", "2026-10-22T09:00:00"}, {"timeZone", "Pacific Standard Time"}}, "Windows zone round trip");
        expectJSON(e["subject"], "Planning", "subject round trip");

        json mailto = graphEvent(vcalendar("ATTENDEE;CUTYPE=ROOM:MAILTO:room9@example.com\r\n"
                                           "ATTENDEE;ROLE=NON-PARTICIPANT:mailto:fyi@example.com\r\n"));
        expectJSON(mailto["attendees"][0], {{"type", "resource"}, {"emailAddress", {{"address", "room9@example.com"}, {"name", ""}}}},
                   "room, MAILTO in capitals");
        expectJSON(mailto["attendees"][1]["type"], "optional", "non-participant is optional");
    }
    {
        // The Repeat control writes only FREQ; the rest comes from DTSTART, Wednesday 7 October.
        expectJSON(recurrenceFor("FREQ=DAILY"),
                   {{"pattern", {{"type", "daily"}, {"interval", 1}}},
                    {"range", {{"type", "noEnd"}, {"startDate", "2026-10-07"}}}}, "daily");
        expectJSON(recurrenceFor("FREQ=WEEKLY")["pattern"],
                   {{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"wednesday"}}, {"firstDayOfWeek", "monday"}},
                   "weekly on the start's weekday");
        expectJSON(recurrenceFor("FREQ=MONTHLY")["pattern"],
                   {{"type", "absoluteMonthly"}, {"interval", 1}, {"dayOfMonth", 7}}, "monthly on the start's day");
        expectJSON(recurrenceFor("FREQ=YEARLY")["pattern"],
                   {{"type", "absoluteYearly"}, {"interval", 1}, {"month", 10}, {"dayOfMonth", 7}}, "yearly on the start's date");
    }
    {
        // Every pattern GraphICS writes reads back as the one it came from.
        expectRoundTrip(series(pattern({{"type", "daily"}, {"interval", 3}})), "daily every 3 days");
        expectRoundTrip(series(pattern({{"type", "relativeMonthly"}, {"interval", 1}, {"daysOfWeek", {"friday"}}, {"index", "last"}},
                                       {{"type", "numbered"}, {"numberOfOccurrences", 10}})), "last Friday, ten times");
        expectRoundTrip(series(pattern({{"type", "relativeMonthly"}, {"interval", 1}, {"index", "first"},
                                        {"daysOfWeek", {"monday", "tuesday", "wednesday", "thursday", "friday"}}})),
                        "first weekday of the month");
        expectRoundTrip(series(pattern({{"type", "absoluteMonthly"}, {"interval", 3}, {"dayOfMonth", 15}})), "every third month on the 15th");
        expectRoundTrip(series(pattern({{"type", "absoluteYearly"}, {"interval", 1}, {"month", 3}, {"dayOfMonth", 14}})), "yearly on March 14");
        expectRoundTrip(series(pattern({{"type", "absoluteMonthly"}, {"interval", 1}, {"dayOfMonth", 31}})), "the 31st");
        expectRoundTrip(series(pattern({{"type", "absoluteYearly"}, {"interval", 1}, {"month", 2}, {"dayOfMonth", 29}})), "February 29");
        expectRoundTrip(series(pattern({{"type", "relativeYearly"}, {"interval", 1}, {"month", 11}, {"daysOfWeek", {"tuesday"}},
                                        {"index", "second"}})), "second Tuesday of November");
        expectRoundTrip(series(pattern({{"type", "weekly"}, {"interval", 2}, {"daysOfWeek", {"monday", "wednesday"}}, {"firstDayOfWeek", "sunday"}},
                                       {{"type", "endDate"}, {"startDate", "2026-10-05"}, {"endDate", "2026-12-16"}})),
                        "biweekly until a date after daylight saving ends");
        expectRoundTrip(series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}}, {"firstDayOfWeek", "sunday"}},
                                       {{"type", "endDate"}, {"startDate", "2026-10-05"}, {"endDate", "2026-12-28"}}),
                               {{"isAllDay", true},
                                {"start", {{"dateTime", "2026-10-05T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                                {"end", {{"dateTime", "2026-10-06T00:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}}}),
                        "all-day until a date", "2026-10-05T07:00:00.0000000");
        // East of UTC an UNTIL lands on the UTC day before the local one.
        expectRoundTrip(series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}}, {"firstDayOfWeek", "sunday"}},
                                       {{"type", "endDate"}, {"startDate", "2026-10-05"}, {"endDate", "2026-11-30"}}),
                               {{"start", {{"dateTime", "2026-10-05T09:00:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}},
                                {"end", {{"dateTime", "2026-10-05T09:30:00.0000000"}, {"timeZone", "Tokyo Standard Time"}}}}),
                        "until a date east of UTC", "2026-10-05T00:00:00.0000000");
    }
    {
        // An UNTIL exactly at the last occurrence's start, which daylight saving puts an hour later
        // in UTC than the series' first: Monday 5 April 2027 at 09:00 Pacific daylight time.
        json e = graphEvent(vcalendar("DTSTART;TZID=Pacific Standard Time:20270104T090000\r\n"
                                      "DTEND;TZID=Pacific Standard Time:20270104T093000\r\n"
                                      "RRULE:FREQ=WEEKLY;BYDAY=MO;UNTIL=20270405T160000Z\r\n"),
                            "2027-01-04T17:00:00.0000000");
        expectJSON(e["recurrence"]["range"], {{"type", "endDate"}, {"startDate", "2027-01-04"}, {"endDate", "2027-04-05"}},
                   "UNTIL at a last occurrence in daylight time");
    }
    {
        expect(refused("FREQ=HOURLY"), "hourly refused");
        expect(refused("FREQ=MONTHLY;BYMONTHDAY=1,15"), "two days of the month refused");
        expect(refused("FREQ=WEEKLY;BYDAY=2MO"), "a weekly position refused");
        expect(refused("FREQ=MONTHLY;BYDAY=1MO,3WE"), "two positioned days refused");
        expect(refused("FREQ=MONTHLY;BYDAY=MO"), "every Monday of the month refused");
        expect(refused("FREQ=DAILY;COUNT=5;UNTIL=20261231T000000Z"), "COUNT with UNTIL refused");
        expect(refused("FREQ=WEEKLY;BYHOUR=9"), "BYHOUR refused");
        expect(refused("FREQ=MONTHLY;BYDAY=MO,TU;BYSETPOS=5"), "a fifth weekday refused");
        expect(!refused("FREQ=MONTHLY;BYDAY=+2TU"), "a signed position read");
    }
    {
        // An override is its own VEVENT, with no recurrence of its own.
        vector<ICSVEvent> vevents = GraphICS::veventsOf(
            "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:abc\r\nDTSTART:20261005T160000Z\r\nDTEND:20261005T163000Z\r\n"
            "RRULE:FREQ=WEEKLY\r\nEXDATE:20261012T160000Z\r\nEND:VEVENT\r\n"
            "BEGIN:VEVENT\r\nUID:abc\r\nRECURRENCE-ID:20261019T160000Z\r\nDTSTART:20261019T170000Z\r\nDTEND:20261019T173000Z\r\n"
            "SUMMARY:Moved\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        expect(vevents.size() == 2, "master and override", to_string(vevents.size()));
        if (vevents.size() == 2) {
            expectEqual(vevents[0].property("EXDATE").value, "20261012T160000Z", "master's EXDATE");
            expectEqual(vevents[1].property("RECURRENCE-ID").value, "20261019T160000Z", "override's RECURRENCE-ID");
            json master = GraphICS::eventForVEvent(vevents[0], "");
            json override = GraphICS::eventForVEvent(vevents[1], "");
            expectJSON(master["recurrence"]["pattern"]["daysOfWeek"], {"monday"}, "master's recurrence");
            expect(!override.count("recurrence"), "override has no recurrence");
            expectJSON(override["subject"], "Moved", "override's own subject");
        }
    }

    // An edit as Graph writes. Each `before` is GraphICS's own copy; each `after` is that copy as
    // the client's edit leaves it.
    {
        json master = series(pattern({{"type", "weekly"}, {"interval", 1}, {"daysOfWeek", {"monday"}}, {"firstDayOfWeek", "sunday"}}),
                             {{"cancelledOccurrences", {"OID.AAMkMaster=.2026-11-02"}},
                              {"exceptionOccurrences", json::array({event({
                                  {"type", "exception"},
                                  {"subject", "Planning (moved)"},
                                  {"originalStart", "2026-10-19T16:00:00Z"},
                                  {"start", {{"dateTime", "2026-10-19T10:00:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                                  {"end", {{"dateTime", "2026-10-19T10:30:00.0000000"}, {"timeZone", "Pacific Standard Time"}}},
                              })})}});
        string base = GraphICS::icsForEvent(master, ME, SERIES_START_UTC);

        expectEqual(writesFor(base, base), "", "an unchanged series writes nothing");
        expectEqual(writesFor(base, edited(base, "SUMMARY:Planning\r\n", "SEQUENCE:1\r\nSUMMARY:Planning\r\n")), "",
                    "a new SEQUENCE alone writes nothing");
        expectEqual(writesFor(base, edited(base, "SUMMARY:Planning\r\n", "SUMMARY:Roadmap\r\n")), "update subject",
                    "a new title sends only the subject");

        string movedAgain = edited(edited(base, "DTSTART;TZID=Pacific Standard Time:20261019T100000", "DTSTART;TZID=Pacific Standard Time:20261019T110000"),
                                   "DTEND;TZID=Pacific Standard Time:20261019T103000", "DTEND;TZID=Pacific Standard Time:20261019T113000");
        expectEqual(writesFor(base, movedAgain), "occurrence 20261019T160000Z end,start", "an override moved again sends its times");
        expectEqual(writesFor(base, edited(base, "SUMMARY:Planning (moved)", "SUMMARY:Planning (room 4)")),
                    "occurrence 20261019T160000Z subject", "an override retitled sends only its subject");

        string reverted = edited(edited(edited(base, "SUMMARY:Planning (moved)", "SUMMARY:Planning"),
                                        "DTSTART;TZID=Pacific Standard Time:20261019T100000", "DTSTART;TZID=Pacific Standard Time:20261019T090000"),
                                 "DTEND;TZID=Pacific Standard Time:20261019T103000", "DTEND;TZID=Pacific Standard Time:20261019T093000");
        expectEqual(writesFor(base, reverted), "occurrence 20261019T160000Z end,start,subject",
                    "an override undone takes the series' values back");

        string added = edited(base, "END:VCALENDAR", "BEGIN:VEVENT\r\nUID:x\r\nRECURRENCE-ID:20261026T160000Z\r\n"
                                                     "DTSTART;TZID=Pacific Standard Time:20261026T140000\r\n"
                                                     "DTEND;TZID=Pacific Standard Time:20261026T143000\r\n"
                                                     "SUMMARY:Planning\r\nTRANSP:OPAQUE\r\nEND:VEVENT\r\nEND:VCALENDAR");
        expectEqual(writesFor(base, added), "occurrence 20261026T160000Z end,start",
                    "a new override sends its times and only the fields it changed from the series");

        expectEqual(writesFor(base, edited(base, "EXDATE;TZID=Pacific Standard Time:20261102T090000",
                                           "EXDATE;TZID=Pacific Standard Time:20261102T090000,20261109T090000")),
                    "cancel Pacific Standard Time:20261109T090000", "a date added to an EXDATE cancels its occurrence");
        expectEqual(writesFor(base, edited(base, "EXDATE;TZID=Pacific Standard Time:20261102T090000\r\n",
                                           "EXDATE;TZID=Pacific Standard Time:20261102T090000\r\nEXDATE;TZID=Pacific Standard Time:20261116T090000\r\n")),
                    "cancel Pacific Standard Time:20261116T090000", "a new EXDATE line cancels its occurrence");
        expectEqual(refusalFor(base, edited(base, "EXDATE;TZID=Pacific Standard Time:20261102T090000\r\n", "")),
                    "cannot-restore-occurrence", "a cancelled occurrence can't come back");

        // The client moves the override's RECURRENCE-ID and the EXDATE along with the series.
        string seriesMoved = base;
        seriesMoved = edited(seriesMoved, "DTSTART;TZID=Pacific Standard Time:20261005T090000", "DTSTART;TZID=Pacific Standard Time:20261005T100000");
        seriesMoved = edited(seriesMoved, "DTEND;TZID=Pacific Standard Time:20261005T093000", "DTEND;TZID=Pacific Standard Time:20261005T103000");
        seriesMoved = edited(seriesMoved, "RECURRENCE-ID:20261019T160000Z", "RECURRENCE-ID:20261019T170000Z");
        seriesMoved = edited(seriesMoved, "EXDATE;TZID=Pacific Standard Time:20261102T090000", "EXDATE;TZID=Pacific Standard Time:20261102T100000");
        expectEqual(writesFor(base, seriesMoved),
                    "update end,isAllDay,recurrence,start\noccurrence 20261019T170000Z end,start,subject\ncancel Pacific Standard Time:20261102T100000",
                    "a series move re-applies its override and its cancelled date");
        expectEqual(writesFor(base, edited(seriesMoved, "SUMMARY:Planning\r\n", "SUMMARY:Roadmap\r\n")),
                    "update end,isAllDay,recurrence,start,subject\noccurrence 20261019T170000Z end,start,subject\ncancel Pacific Standard Time:20261102T100000",
                    "a series moved and retitled at once");

        string nextDay = base;
        nextDay = edited(nextDay, "DTSTART;TZID=Pacific Standard Time:20261005T090000", "DTSTART;TZID=Pacific Standard Time:20261006T090000");
        nextDay = edited(nextDay, "DTEND;TZID=Pacific Standard Time:20261005T093000", "DTEND;TZID=Pacific Standard Time:20261006T093000");
        nextDay = edited(nextDay, "RECURRENCE-ID:20261019T160000Z", "RECURRENCE-ID:20261020T160000Z");
        nextDay = edited(nextDay, "EXDATE;TZID=Pacific Standard Time:20261102T090000", "EXDATE;TZID=Pacific Standard Time:20261103T090000");
        expectEqual(writesFor(base, nextDay),
                    "update end,isAllDay,recurrence,start\noccurrence 20261020T160000Z end,start,subject\ncancel Pacific Standard Time:20261103T090000",
                    "a series moved a day takes its cancelled date along, which is no restore");

        // Longer occurrences: the override and the cancelled date keep their slots and are re-applied.
        string longer = edited(base, "DTEND;TZID=Pacific Standard Time:20261005T093000", "DTEND;TZID=Pacific Standard Time:20261005T100000");
        expectEqual(writesFor(base, longer),
                    "update end,isAllDay,recurrence,start\noccurrence 20261019T160000Z end,start,subject\ncancel Pacific Standard Time:20261102T090000",
                    "a series made longer re-applies its override and its cancelled date");
        expectEqual(refusalFor("", base), "", "nothing refused");
        expectEqual(writesFor("", base),
                    "create attendees,body,end,isAllDay,location,recurrence,showAs,start,subject\n"
                    "occurrence 20261019T160000Z end,start,subject\ncancel Pacific Standard Time:20261102T090000",
                    "a new series is created, then its override and cancelled date");
    }
    {
        string base = GraphICS::icsForEvent(event({
            {"showAs", "tentative"},
            {"attendees", json::array({
                {{"type", "required"}, {"status", {{"response", "accepted"}}},
                 {"emailAddress", {{"name", "Doe, Jane"}, {"address", "jane@example.com"}}}},
                {{"type", "optional"}, {"status", {{"response", "none"}}},
                 {"emailAddress", {{"name", "Sam"}, {"address", "sam@example.com"}}}},
            })},
        }), ME, START_UTC);
        const string jane = "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;CN=\"Doe, Jane\":mailto:jane@example.com";

        // Show As "tentative" reads as OPAQUE, which the client sends back untouched.
        expectEqual(writesFor(base, base), "", "an unchanged event writes nothing");
        expectEqual(writesFor(base, edited(base, jane, "ATTENDEE;PARTSTAT=ACCEPTED;CN=Jane Doe:MAILTO:Jane@Example.com")), "",
                    "a guest's name and capitals are no change");
        expectEqual(writesFor(base, edited(base, jane, jane + "\r\nATTENDEE;CN=Kim:mailto:kim@example.com")), "update attendees",
                    "a new guest updates the attendees");
        expectEqual(writesFor(base, edited(base, jane, "ATTENDEE;ROLE=OPT-PARTICIPANT;CN=\"Doe, Jane\":mailto:jane@example.com")),
                    "update attendees", "a guest made optional updates the attendees");
        expectEqual(writesFor(base, edited(base, "TRANSP:OPAQUE", "TRANSP:TRANSPARENT")), "update showAs", "Show As free");

        string moved = edited(edited(base, "DTSTART;TZID=Pacific Standard Time:20261022T090000", "DTSTART;TZID=America/Los_Angeles:20261023T090000"),
                              "DTEND;TZID=Pacific Standard Time:20261022T093000", "DTEND;TZID=America/Los_Angeles:20261023T093000");
        expectEqual(writesFor(base, moved), "update end,isAllDay,start", "a moved event sends its times, and no recurrence");
        expectEqual(writesFor(base, edited(base, "TRANSP:OPAQUE", "RRULE:FREQ=WEEKLY\r\nTRANSP:OPAQUE")), "update end,isAllDay,recurrence,start",
                    "a repeat added sends the recurrence with the times");
        expectEqual(refusalFor(base, edited(base, "TRANSP:OPAQUE", "RRULE:FREQ=HOURLY\r\nTRANSP:OPAQUE")), "unsupported-recurrence",
                    "a repeat Graph can't express is refused");
    }

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "graph_ics_test: all passed\n";
    return 0;
}
