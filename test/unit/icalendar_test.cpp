//
//  icalendar_test.cpp
//  Unit tests for when a VEVENT ends (RFC 5545 section 3.6.1): DTEND, else DTSTART plus DURATION,
//  else one day after a DATE start, and the DURATION grammar of section 3.3.6.
//
//  Build and run (Linux): cmake . && make icalendar_test && ./icalendar_test
//

#include "icalendar.h"

#include <iostream>

static int failures = 0;

static void expect(bool ok, const string & label, const string & detail = "") {
    if (!ok) {
        cerr << "FAIL " << label << (detail.empty() ? "" : ": " + detail) << "\n";
        failures++;
    }
}

static void expectEqual(long actual, long expected, const string & label) {
    expect(actual == expected, label, "expected " + to_string(expected) + ", got " + to_string(actual));
}

static long utc(int year, int month, int day, int hour, int minute) {
    struct tm parts {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    return (long)timegm(&parts);
}

/** The local midnight starting a DATE, as icalendarlib reads one. */
static long dayStart(const string & date) {
    Date value;
    value = date;
    return value.toUnix();
}

static ICalendarEvent * eventIn(ICalendar & calendar, const string & properties, const string & zones = "") {
    calendar.LoadFromString("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + zones +
                            "BEGIN:VEVENT\r\nUID:icalendar-test\r\n" + properties + "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    return calendar.Events.empty() ? nullptr : calendar.Events.front();
}

static long endOf(const string & properties, const string & zones = "") {
    ICalendar calendar("");
    ICalendarEvent * event = eventIn(calendar, properties, zones);
    return event ? (long)event->EndUnix() : -1;
}

static void expectDuration(const string & value, long long days, long long seconds) {
    long long gotDays = -1, gotSeconds = -1;
    bool ok = ParseDuration(value, gotDays, gotSeconds);
    expect(ok, "reads " + value);
    expect(gotDays == days && gotSeconds == seconds, "reads " + value + " as " + to_string(days) + " days and " +
           to_string(seconds) + " seconds", "got " + to_string(gotDays) + " days and " + to_string(gotSeconds) + " seconds");
}

static void expectNoDuration(const string & value) {
    long long days = 0, seconds = 0;
    expect(!ParseDuration(value, days, seconds), "refuses \"" + value + "\"");
}

// New York's rules under a name no zone table has, so the VTIMEZONE itself is read.
static const string LAB_EASTERN =
    "BEGIN:VTIMEZONE\r\nTZID:Lab/Eastern\r\n"
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\nDTSTART:19700308T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU\r\nEND:DAYLIGHT\r\n"
    "BEGIN:STANDARD\r\nTZOFFSETFROM:-0400\r\nTZOFFSETTO:-0500\r\nDTSTART:19701101T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n";

int main() {
    expectDuration("PT1H30M", 0, 5400);
    expectDuration("P1DT12H", 1, 43200);
    expectDuration("P2W", 14, 0);
    expectDuration("PT90M", 0, 5400);
    expectDuration("P1D", 1, 0);
    expectDuration("PT3600S", 0, 3600);
    expectDuration("+PT1H", 0, 3600);
    expectDuration("P1DT1H1M1S", 1, 3661);
    expectDuration("PT0S", 0, 0);
    expectDuration("P123456789D", 123456789, 0);
    expectDuration("PT999999999H", 0, 3599999996400LL);

    expectNoDuration("-PT1H");
    expectNoDuration("");
    expectNoDuration("P");
    expectNoDuration("PT");
    expectNoDuration("P1DT");
    expectNoDuration("T1H");
    expectNoDuration("1H");
    expectNoDuration("P1H");
    expectNoDuration("PT1D");
    expectNoDuration("P1W1D");
    expectNoDuration("P1WT1H");
    expectNoDuration("P1D1D");
    expectNoDuration("PT1M1H");
    expectNoDuration("PT1HT1M");
    expectNoDuration("P1.5D");
    expectNoDuration("PT1HX");
    expectNoDuration("PD");
    expectNoDuration("P1");
    expectNoDuration("PT1X");
    expectNoDuration("PT1X1H");
    expectNoDuration("P1234567890D");

    expectEqual(endOf("DTSTART:20260301T100000Z\r\nDTEND:20260301T110000Z\r\nDURATION:PT5H"),
                utc(2026, 3, 1, 11, 0), "ends at DTEND when the event has one");
    expectEqual(endOf("DTSTART:20260301T100000Z\r\nDURATION:PT1H30M"),
                utc(2026, 3, 1, 11, 30), "ends DURATION after a DATE-TIME start");
    expectEqual(endOf("DTSTART:20260301T100000Z\r\nDURATION:P1DT12H"),
                utc(2026, 3, 2, 22, 0), "adds the days and the time of a DURATION");
    expectEqual(endOf("DTSTART:20260301T100000Z"), utc(2026, 3, 1, 10, 0),
                "ends a DATE-TIME start with neither DTEND nor DURATION at its start");
    expectEqual(endOf("DTSTART:20260301T100000Z\r\nDURATION:PT1D"), utc(2026, 3, 1, 10, 0),
                "reads a malformed DURATION as none");
    expectEqual(endOf("DTSTART:20260301T100000Z\r\nDURATION:PT1HX"), utc(2026, 3, 1, 10, 0),
                "reads a DURATION malformed after a valid part as none");

    // Lab/Eastern sets its clocks back at 02:00 on 1 November 2026: a day across it is 25 hours.
    expectEqual(endOf("DTSTART;TZID=Lab/Eastern:20261031T090000\r\nDURATION:P1D", LAB_EASTERN),
                utc(2026, 11, 1, 14, 0), "adds a DURATION day as a calendar day of the start's zone");
    expectEqual(endOf("DTSTART;TZID=Lab/Eastern:20261031T090000\r\nDURATION:PT24H", LAB_EASTERN),
                utc(2026, 11, 1, 13, 0), "adds a DURATION time part as exact seconds");

    expectEqual(endOf("DTSTART;VALUE=DATE:20260301"), dayStart("20260302"),
                "ends a DATE start with neither DTEND nor DURATION the next day");
    expectEqual(endOf("DTSTART;VALUE=DATE:20260301\r\nDURATION:P3D"), dayStart("20260304"),
                "ends a DATE start DURATION days later");
    expectEqual(endOf("DTSTART;VALUE=DATE:20260301\r\nDURATION:P1H"), dayStart("20260302"),
                "reads a malformed DURATION on a DATE start as none");
    expectEqual(endOf("DTSTART;VALUE=DATE:20260101\r\nDURATION:P40000D"), dayStart("21350709"),
                "adds a DURATION of more days than a short holds");

    ICalendar withAlarm("");
    ICalendarEvent * alarmed = eventIn(withAlarm, "DTSTART:20260301T100000Z\r\nDTEND:20260301T110000Z\r\n"
                                                  "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\n"
                                                  "DURATION:PT5M\r\nREPEAT:2\r\nEND:VALARM");
    expect(alarmed && alarmed->Duration.empty(), "leaves an alarm's DURATION off the event");

    ICalendar timed("");
    ICalendarEvent * event = eventIn(timed, "DTSTART:20260301T100000Z\r\nDURATION:PT1H");
    expect(event && event->Duration == "PT1H", "keeps the DURATION as written");
    string written = event ? (string)*event : "";
    expect(written.find("\r\nDURATION:PT1H\r\n") != string::npos, "writes the DURATION back", written);
    ICalendarEvent copy(*event);
    expect(copy.Duration == "PT1H", "copies the DURATION with the event");

    ICalendar both("");
    event = eventIn(both, "DTSTART:20260301T100000Z\r\nDTEND:20260301T110000Z\r\nDURATION:PT5H");
    written = event ? (string)*event : "";
    expect(written.find("DURATION") == string::npos, "writes DTEND, never both", written);

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "All icalendar tests passed\n";
    return 0;
}
