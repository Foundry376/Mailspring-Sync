//
//  graph_ics_test.cpp
//  Unit tests for turning Microsoft Graph events into iCalendar resources. Each case also
//  parses the output with icalendarlib, the engine's own reader of Event.ics.
//
//  Build and run (Linux): cmake . && make graph_ics_test && ./graph_ics_test
//

#include "GraphICS.hpp"
#include "icalendar.h"

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

static const string ME = "me@example.test";
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
        // The series starts in daylight time (UTC-7) and ends after it (UTC-8): the last
        // occurrence, 09:00 on Wednesday 2026-12-16, is 17:00 UTC. UNTIL taken at the start's
        // offset alone would be 16:00 UTC and drop it.
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
        // An all-day series ends on a date.
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
        // Edited occurrences become VEVENTs of the master's UID, keyed by their original start in
        // UTC; cancelled ones become EXDATEs at the series' wall-clock start in its zone.
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

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "graph_ics_test: all passed\n";
    return 0;
}
