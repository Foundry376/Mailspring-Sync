//
//  GraphICS.cpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "GraphICS.hpp"
#include "JSONUtils.hpp"
#include "strlib.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <vector>

#ifdef _MSC_VER
#define timegm _mkgmtime
#endif

static const json & child(const json & object, const string & key) {
    static const json none = json::object();
    return object.is_object() && object.count(key) && object[key].is_object() ? object[key] : none;
}

static bool flag(const json & object, const string & key) {
    return object.is_object() && object.count(key) && object[key].is_boolean() && object[key].get<bool>();
}

// RFC 5545 section 3.3.11.
static string escapeText(const string & text) {
    string out;
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case ';': out += "\\;"; break;
            case ',': out += "\\,"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            default: out += c;
        }
    }
    return out;
}

// A parameter value holding ':', ';' or ',' is quoted, and cannot contain DQUOTE (RFC 5545 section 3.2).
static string paramValue(string value) {
    value.erase(remove(value.begin(), value.end(), '"'), value.end());
    return value.find_first_of(":;,") != string::npos ? "\"" + value + "\"" : value;
}

// Folds at 75 octets (RFC 5545 section 3.1), never inside a UTF-8 sequence.
static void appendLine(string & ics, const string & line) {
    size_t start = 0;
    size_t limit = 75;
    while (line.size() - start > limit) {
        size_t end = start + limit;
        while (end > start && ((unsigned char)line[end] & 0xC0) == 0x80) {
            end--;
        }
        ics += line.substr(start, end - start) + "\r\n ";
        start = end;
        limit = 74; // the continuation's leading space counts
    }
    ics += line.substr(start) + "\r\n";
}

// Graph's "2026-10-22T09:00:00.0000000" as 20261022T090000, or "" when malformed.
static string basicDateTime(const string & extended) {
    if (extended.size() < 19) {
        return "";
    }
    return extended.substr(0, 4) + extended.substr(5, 2) + extended.substr(8, 2) + "T" +
           extended.substr(11, 2) + extended.substr(14, 2) + extended.substr(17, 2);
}

// A Graph date-time or date ("2026-10-22T09:00:00.0000000", "2026-12-31") read as UTC.
static time_t secondsAsUTC(const string & extended) {
    struct tm parts {};
    if (sscanf(extended.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday,
               &parts.tm_hour, &parts.tm_min, &parts.tm_sec) < 3) {
        return 0;
    }
    parts.tm_year -= 1900;
    parts.tm_mon -= 1;
    return timegm(&parts);
}

static string formatUTC(time_t seconds, const char * format) {
    struct tm parts {};
#ifdef _MSC_VER
    gmtime_s(&parts, &seconds);
#else
    gmtime_r(&seconds, &parts);
#endif
    char out[20];
    strftime(out, sizeof(out), format, &parts);
    return out;
}

// `value` (20261022T090000) as a DTSTART, DTEND or EXDATE line in `zone`, or its date when all-day.
static string dateTimeLine(const string & name, const string & value, const string & zone, bool allDay) {
    if (allDay) {
        return name + ";VALUE=DATE:" + value.substr(0, 8);
    }
    if (zone == "UTC") {
        return name + ":" + value + "Z";
    }
    return name + ";TZID=" + paramValue(zone) + ":" + value;
}

static string dateTimeProperty(const string & name, const json & dateTimeTimeZone, bool allDay) {
    return dateTimeLine(name, basicDateTime(JSONString(dateTimeTimeZone, "dateTime")),
                        JSONString(dateTimeTimeZone, "timeZone"), allDay);
}

static int number(const json & object, const string & key) {
    return object.is_object() && object.count(key) && object[key].is_number_integer() ? object[key].get<int>() : 0;
}

static const map<string, string> WEEKDAYS = {
    {"sunday", "SU"}, {"monday", "MO"}, {"tuesday", "TU"}, {"wednesday", "WE"},
    {"thursday", "TH"}, {"friday", "FR"}, {"saturday", "SA"},
};
static const map<string, string> SET_POSITIONS = {
    {"first", "1"}, {"second", "2"}, {"third", "3"}, {"fourth", "4"}, {"last", "-1"},
};

// UNTIL this long after the last occurrence's local start still precedes the next occurrence,
// and covers a daylight-saving change between the series' start and its end.
static const time_t UNTIL_MARGIN = 2 * 60 * 60;

// Graph's patternedRecurrence as an RRULE (https://learn.microsoft.com/en-us/graph/api/resources/patternedrecurrence).
static string rruleLine(const json & recurrence, const json & start, bool allDay, time_t utcOffset) {
    const json & pattern = child(recurrence, "pattern");
    const json & range = child(recurrence, "range");
    string type = JSONString(pattern, "type");
    bool monthly = type == "absoluteMonthly" || type == "relativeMonthly";
    bool yearly = type == "absoluteYearly" || type == "relativeYearly";
    bool relative = type == "relativeMonthly" || type == "relativeYearly";

    string rule = type == "daily" ? "FREQ=DAILY" : type == "weekly" ? "FREQ=WEEKLY"
                : monthly ? "FREQ=MONTHLY" : yearly ? "FREQ=YEARLY" : "";
    if (rule == "") {
        return "";
    }
    if (number(pattern, "interval") > 1) {
        rule += ";INTERVAL=" + to_string(number(pattern, "interval"));
    }
    if (yearly) {
        rule += ";BYMONTH=" + to_string(number(pattern, "month"));
    }
    if (type == "absoluteMonthly" || type == "absoluteYearly") {
        rule += ";BYMONTHDAY=" + to_string(number(pattern, "dayOfMonth"));
    }

    if (type == "weekly" || relative) {
        string days;
        size_t dayCount = 0;
        if (pattern.count("daysOfWeek") && pattern["daysOfWeek"].is_array()) {
            for (const auto & day : pattern["daysOfWeek"]) {
                auto it = WEEKDAYS.find(day.is_string() ? day.get<string>() : "");
                if (it != WEEKDAYS.end()) {
                    days += (dayCount++ ? "," : "") + it->second;
                }
            }
        }
        auto position = SET_POSITIONS.find(JSONString(pattern, "index"));
        string setPosition = position != SET_POSITIONS.end() ? position->second : "1";
        if (relative && dayCount == 1) {
            rule += ";BYDAY=" + setPosition + days;
        } else {
            rule += ";BYDAY=" + days + (relative ? ";BYSETPOS=" + setPosition : "");
        }
    }
    if (type == "weekly") {
        auto weekStart = WEEKDAYS.find(JSONString(pattern, "firstDayOfWeek"));
        if (weekStart != WEEKDAYS.end()) {
            rule += ";WKST=" + weekStart->second;
        }
    }

    string rangeType = JSONString(range, "type");
    if (rangeType == "numbered") {
        rule += ";COUNT=" + to_string(number(range, "numberOfOccurrences"));
    } else if (rangeType == "endDate") {
        // endDate is the inclusive last local date; a DATE-TIME UNTIL must be UTC (RFC 5545 section 3.3.10).
        time_t endDate = secondsAsUTC(JSONString(range, "endDate"));
        if (allDay) {
            rule += ";UNTIL=" + formatUTC(endDate, "%Y%m%d");
        } else {
            time_t timeOfDay = secondsAsUTC(JSONString(start, "dateTime")) % 86400;
            rule += ";UNTIL=" + formatUTC(endDate + timeOfDay - utcOffset + UNTIL_MARGIN, "%Y%m%dT%H%M%SZ");
        }
    }
    return "RRULE:" + rule;
}

// cancelledOccurrences hold "OID.{seriesMasterId}.{date}", the date in the series' zone.
static string exdateLine(const string & occurrenceId, const json & start, bool allDay) {
    string date = occurrenceId.substr(occurrenceId.rfind('.') + 1);
    string startValue = basicDateTime(JSONString(start, "dateTime"));
    string timeOfDay = startValue.size() > 8 ? startValue.substr(8) : "";
    return dateTimeLine("EXDATE", formatUTC(secondsAsUTC(date), "%Y%m%d") + timeOfDay,
                        JSONString(start, "timeZone"), allDay);
}

static string recurrenceIdLine(const json & exception, bool allDay, time_t utcOffset) {
    string originalStart = JSONString(exception, "originalStart");
    if (allDay) {
        // An all-day occurrence starts at local midnight: the nearest one absorbs a daylight-saving
        // change since the series' start.
        time_t local = secondsAsUTC(originalStart) + utcOffset;
        return "RECURRENCE-ID;VALUE=DATE:" + formatUTC((local + 43200) / 86400 * 86400, "%Y%m%d");
    }
    return "RECURRENCE-ID:" + basicDateTime(originalStart) + "Z";
}

static string partstatFor(const string & response) {
    if (response == "accepted" || response == "organizer") return "ACCEPTED";
    if (response == "tentativelyAccepted") return "TENTATIVE";
    if (response == "declined") return "DECLINED";
    return "NEEDS-ACTION";
}

static string participant(const string & property, const string & params, const json & emailAddress) {
    string name = JSONString(emailAddress, "name");
    string cn = name == "" ? "" : ";CN=" + paramValue(name);
    return property + params + cn + ":mailto:" + JSONString(emailAddress, "address");
}

static void appendVEvent(string & ics, const json & event, const string & uid, const string & mailboxAddress,
                         const vector<string> & recurrenceLines) {
    bool allDay = flag(event, "isAllDay");
    appendLine(ics, "BEGIN:VEVENT");
    appendLine(ics, "UID:" + uid);
    appendLine(ics, "DTSTAMP:" + basicDateTime(JSONString(event, "lastModifiedDateTime")) + "Z");
    appendLine(ics, dateTimeProperty("DTSTART", child(event, "start"), allDay));
    appendLine(ics, dateTimeProperty("DTEND", child(event, "end"), allDay));
    for (const auto & line : recurrenceLines) {
        appendLine(ics, line);
    }
    appendLine(ics, "SUMMARY:" + escapeText(JSONString(event, "subject")));

    string location = JSONString(child(event, "location"), "displayName");
    if (location != "") {
        appendLine(ics, "LOCATION:" + escapeText(location));
    }
    string description = JSONString(child(event, "body"), "content");
    if (description != "") {
        appendLine(ics, "DESCRIPTION:" + escapeText(description));
    }

    appendLine(ics, string("STATUS:") + (flag(event, "isCancelled") ? "CANCELLED" : "CONFIRMED"));
    appendLine(ics, string("TRANSP:") + (JSONString(event, "showAs") == "free" ? "TRANSPARENT" : "OPAQUE"));

    const json & organizer = child(child(event, "organizer"), "emailAddress");
    if (JSONString(organizer, "address") != "") {
        appendLine(ics, participant("ORGANIZER", "", organizer));
    }

    // The mailbox's own entry in `attendees` can lag behind its answer; responseStatus doesn't.
    string myResponse = JSONString(child(event, "responseStatus"), "response");
    if (event.count("attendees") && event["attendees"].is_array()) {
        for (const auto & attendee : event["attendees"]) {
            const json & emailAddress = child(attendee, "emailAddress");
            string address = JSONString(emailAddress, "address");
            if (address == "") {
                continue;
            }
            bool mine = myResponse != "" && equalsIgnoreCase(address, mailboxAddress);
            string response = mine ? myResponse : JSONString(child(attendee, "status"), "response");
            string type = JSONString(attendee, "type");
            string role = type == "resource" ? ";CUTYPE=RESOURCE;ROLE=NON-PARTICIPANT"
                        : type == "optional" ? ";ROLE=OPT-PARTICIPANT"
                        : ";ROLE=REQ-PARTICIPANT";
            appendLine(ics, participant("ATTENDEE", role + ";PARTSTAT=" + partstatFor(response), emailAddress));
        }
    }
    appendLine(ics, "END:VEVENT");
}

string GraphICS::icsForEvent(const json & event, const string & mailboxAddress, const string & startUTC) {
    // Occurrences carry their own iCalUId; the series shares the master's.
    string uid = JSONString(event, "iCalUId");
    bool allDay = flag(event, "isAllDay");
    const json & start = child(event, "start");
    time_t utcOffset = secondsAsUTC(JSONString(start, "dateTime")) - secondsAsUTC(startUTC);

    vector<string> seriesLines;
    const json & recurrence = child(event, "recurrence");
    if (!recurrence.empty()) {
        string rrule = rruleLine(recurrence, start, allDay, utcOffset);
        if (rrule != "") {
            seriesLines.push_back(rrule);
        }
        if (event.count("cancelledOccurrences") && event["cancelledOccurrences"].is_array()) {
            for (const auto & occurrenceId : event["cancelledOccurrences"]) {
                if (occurrenceId.is_string()) {
                    seriesLines.push_back(exdateLine(occurrenceId.get<string>(), start, allDay));
                }
            }
        }
    }

    string ics;
    appendLine(ics, "BEGIN:VCALENDAR");
    appendLine(ics, "VERSION:2.0");
    appendLine(ics, "PRODID:-//Mailspring//Microsoft Graph//EN");
    appendVEvent(ics, event, uid, mailboxAddress, seriesLines);
    if (event.count("exceptionOccurrences") && event["exceptionOccurrences"].is_array()) {
        for (const auto & exception : event["exceptionOccurrences"]) {
            appendVEvent(ics, exception, uid, mailboxAddress, {recurrenceIdLine(exception, allDay, utcOffset)});
        }
    }
    appendLine(ics, "END:VCALENDAR");
    return ics;
}
