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
#include <sstream>

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

static string unfold(string ics) {
    for (size_t at = ics.find("\r\n "); at != string::npos; at = ics.find("\r\n ", at)) {
        ics.erase(at, 3);
    }
    return ics;
}

static void expectLine(const string & ics, const string & line, const string & label) {
    expect(("\r\n" + unfold(ics)).find("\r\n" + line + "\r\n") != string::npos, label,
           "no line \"" + line + "\" in\n" + unfold(ics));
}

static json event(json fields) {
    json e = {
        {"iCalUId", "040000008200E00074C5B7101A82E0080000000011"},
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

int main() {
    {
        // Fetched in its own zone, a timed event keeps Graph's Windows zone name as its TZID.
        string ics = GraphICS::icsForEvent(event({}), ME);
        expectLine(ics, "DTSTART;TZID=Pacific Standard Time:20261022T090000", "zoned DTSTART");
        expectLine(ics, "DTEND;TZID=Pacific Standard Time:20261022T093000", "zoned DTEND");
        expectLine(ics, "DTSTAMP:20261020T175034Z", "DTSTAMP from lastModifiedDateTime");
        expectLine(ics, "STATUS:CONFIRMED", "confirmed");
        expectLine(ics, "TRANSP:OPAQUE", "busy is opaque");
        ICalendar cal(ics);
        expect(cal.Events.size() == 1, "one VEVENT");
        if (!cal.Events.empty()) {
            ICalendarEvent * e = cal.Events.front();
            expectEqual(e->UID, "040000008200E00074C5B7101A82E0080000000011", "parsed UID");
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
        }), ME);
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
        }), ME);
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
        }), ME);
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
            })},
        }), ME);
        expectLine(ics, "ORGANIZER;CN=Pat Lee:mailto:pat@example.com", "organizer");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;CN=\"Doe, Jane\":mailto:jane@example.com",
                   "accepted, name with a comma quoted");
        expectLine(ics, "ATTENDEE;ROLE=OPT-PARTICIPANT;PARTSTAT=TENTATIVE;CN=Sam:mailto:sam@example.com", "tentative");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION:mailto:kim@example.com", "unanswered, no name");
        expectLine(ics, "ATTENDEE;CUTYPE=RESOURCE;ROLE=NON-PARTICIPANT;PARTSTAT=NEEDS-ACTION;CN=Room 4:mailto:room4@example.com",
                   "resource");
        expectLine(ics, "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=DECLINED;CN=Me:mailto:ME@example.test",
                   "own answer from responseStatus");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expect(cal.Events.front()->Attendees.size() == 5, "parsed attendees",
                   to_string(cal.Events.front()->Attendees.size()));
        }
    }
    {
        string ics = GraphICS::icsForEvent(event({{"isCancelled", true}, {"showAs", "free"}}), ME);
        expectLine(ics, "STATUS:CANCELLED", "cancelled");
        expectLine(ics, "TRANSP:TRANSPARENT", "free is transparent");
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expectEqual(cal.Events.front()->Status, "CANCELLED", "parsed STATUS");
        }
    }
    {
        // Text is escaped, and long lines fold at 75 octets without splitting a UTF-8 character.
        string notes;
        for (int i = 0; i < 40; i++) {
            notes += "Café ☕ ";
        }
        string ics = GraphICS::icsForEvent(event({
            {"subject", "Budget, Q4; draft \\ v2"},
            {"location", {{"displayName", "Room 4, Building 2"}}},
            {"body", {{"contentType", "text"}, {"content", "Agenda:\r\n1. Numbers\r\n" + notes}}},
        }), ME);
        expectLine(ics, "SUMMARY:Budget\\, Q4\\; draft \\\\ v2", "SUMMARY escaped");
        expectLine(ics, "DESCRIPTION:Agenda:\\n1. Numbers\\n" + notes, "DESCRIPTION escaped and unfolds whole");

        istringstream lines(ics);
        string line;
        while (getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            expect(line.size() <= 75, "line within 75 octets", to_string(line.size()) + ": " + line);
            if (line.size() > 1 && line[0] == ' ') {
                expect(((unsigned char)line[1] & 0xC0) != 0x80, "fold not inside a UTF-8 character", line);
            }
        }
        ICalendar cal(ics);
        if (!cal.Events.empty()) {
            expectEqual(cal.Events.front()->Location, "Room 4, Building 2", "parsed LOCATION unescaped");
        }
    }

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "graph_ics_test: all passed\n";
    return 0;
}
