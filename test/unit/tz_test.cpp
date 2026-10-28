//
//  tz_test.cpp
//  Unit tests for reading a TZID'd DATE-TIME: ICU's zone, read from the shipped tzdata, outranks
//  the VTIMEZONE an object carries, a name ICU lacks reads through that VTIMEZONE, and RFC 5545
//  section 3.3.5 decides wall clocks a change skips or repeats.
//
//  Build and run (Linux): cmake . && make tz_test && ./tz_test Vendor/icu-tzdata
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

static ICalendarEvent * eventIn(ICalendar & calendar, const string & dtstart, const string & zones = "") {
    calendar.LoadFromString("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + zones +
                            "BEGIN:VEVENT\r\nUID:tz-test\r\n" + dtstart + "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    return calendar.Events.empty() ? nullptr : calendar.Events.front();
}

static long startOf(const string & dtstart, const string & zones = "") {
    ICalendar calendar("");
    ICalendarEvent * event = eventIn(calendar, dtstart, zones);
    return event ? event->DtStart.toUnix() : -1;
}

// Mexico City as invitations written before 2022 describe it, still observing DST.
static const string STALE_MEXICO_CITY =
    "BEGIN:VTIMEZONE\r\nTZID:America/Mexico_City\r\n"
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:-0600\r\nTZOFFSETTO:-0500\r\nDTSTART:19700405T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=4;BYDAY=1SU\r\nEND:DAYLIGHT\r\n"
    "BEGIN:STANDARD\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0600\r\nDTSTART:19701025T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n";

int main(int argc, const char * argv[]) {
    if (argc > 1) {
        SystemTimeZone::UseZoneFilesIn(argv[1]);
    }
    string version = SystemTimeZone::DataVersion();
    cout << "tzdata " << version << "\n";
    expect(version >= "2022g", "reads tzdata from after Mexico's 2022 change", version);

    expectEqual(startOf("DTSTART;TZID=America/Mexico_City:20260701T120000", STALE_MEXICO_CITY),
                utc(2026, 7, 1, 18, 0), "reads a zone ICU knows through tzdata, not a stale VTIMEZONE");
    expectEqual(startOf("DTSTART;TZID=Eastern Standard Time:20260701T120000"),
                utc(2026, 7, 1, 16, 0), "maps a Windows name to its IANA zone");
    expectEqual(startOf("DTSTART;TZID=EST:20260701T120000"),
                utc(2026, 7, 1, 17, 0), "reads EST as tzdata's fixed -05:00, not as New York");
    expectEqual(startOf("DTSTART;TZID=\"Customized Time Zone\":20260701T120000",
                        "BEGIN:VTIMEZONE\r\nTZID:Customized Time Zone\r\nBEGIN:STANDARD\r\n"
                        "DTSTART:16010101T000000\r\nTZOFFSETFROM:+0530\r\nTZOFFSETTO:+0530\r\n"
                        "END:STANDARD\r\nEND:VTIMEZONE\r\n"),
                utc(2026, 7, 1, 6, 30), "reads a name ICU lacks through the object's VTIMEZONE");
    expectEqual(startOf("DTSTART;TZID=UTC:20260701T120000"), utc(2026, 7, 1, 12, 0), "reads TZID=UTC as UTC");

    // New York sets its clocks forward at 02:00 on 8 March 2026 and back at 02:00 on 1 November.
    expectEqual(startOf("DTSTART;TZID=America/New_York:20260308T023000"),
                utc(2026, 3, 8, 7, 30), "reads a skipped wall clock in the offset before the gap");
    expectEqual(startOf("DTSTART;TZID=America/New_York:20261101T013000"),
                utc(2026, 11, 1, 5, 30), "reads a repeated wall clock as its first occurrence");

    ICalendar unknown("");
    ICalendarEvent * floating = eventIn(unknown, "DTSTART;TZID=Mars/Olympus_Mons:20260701T120000");
    expect(floating && !floating->DtStart.Zone, "leaves a name nobody defines without a zone");
    expect(!SystemTimeZone::Named("Mars/Olympus_Mons") && !SystemTimeZone::Named("Mars/Olympus_Mons"),
           "finds no zone for an unknown name, every time it is asked");
    expect(SystemTimeZone::Named("America/Chicago") == SystemTimeZone::Named("America/Chicago"),
           "shares one zone per name");

    if (failures) {
        cerr << failures << " failure(s)\n";
        return 1;
    }
    cout << "All tz tests passed\n";
    return 0;
}
