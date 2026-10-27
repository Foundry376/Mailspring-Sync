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
#include "icalendar.h"
#include "strlib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#ifdef _MSC_VER
#define timegm _mkgmtime
#endif

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

// Unfolded, which RFC 5545 section 3.1 allows: icalendarlib keeps a fold's \r inside the value.
static void appendLine(string & ics, const string & line) {
    ics += line + "\r\n";
}

// Graph's "2026-10-22T09:00:00.0000000" as 20261022T090000, or "" when malformed.
static string basicDateTime(const string & extended) {
    if (extended.size() < 19) {
        return "";
    }
    return extended.substr(0, 4) + extended.substr(5, 2) + extended.substr(8, 2) + "T" +
           extended.substr(11, 2) + extended.substr(14, 2) + extended.substr(17, 2);
}

time_t GraphICS::secondsAsUTC(const string & extended) {
    struct tm parts {};
    if (sscanf(extended.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday,
               &parts.tm_hour, &parts.tm_min, &parts.tm_sec) < 3) {
        return 0;
    }
    parts.tm_year -= 1900;
    parts.tm_mon -= 1;
    return timegm(&parts);
}

// A Graph DateTimeOffset ("2026-11-02T12:00:00-06:00", "2026-10-19T16:00:00Z") as a UTC instant.
// Graph writes originalStart in the zone Prefer: outlook.timezone names, though the reference says UTC.
static time_t secondsOfDateTimeOffset(const string & value) {
    time_t seconds = GraphICS::secondsAsUTC(value);
    size_t sign = value.find_first_of("+-", 19);
    int hours = 0, minutes = 0;
    if (sign != string::npos && sscanf(value.c_str() + sign + 1, "%2d:%2d", &hours, &minutes) == 2) {
        time_t offset = hours * 3600 + minutes * 60;
        seconds += value[sign] == '+' ? -offset : offset;
    }
    return seconds;
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

static const map<string, string> WEEKDAYS = {
    {"sunday", "SU"}, {"monday", "MO"}, {"tuesday", "TU"}, {"wednesday", "WE"},
    {"thursday", "TH"}, {"friday", "FR"}, {"saturday", "SA"},
};
static const map<string, string> SET_POSITIONS = {
    {"first", "1"}, {"second", "2"}, {"third", "3"}, {"fourth", "4"}, {"last", "-1"},
};

// The most days `month` can have; month 0, a monthly pattern, can have 31.
static int longestMonth(int month) {
    static const int LONGEST[] = {31, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month >= 1 && month <= 12 ? LONGEST[month] : LONGEST[0];
}

// Exchange moves a day past the month's end to its last day (EWS AbsoluteMonthlyRecurrence); -1 says
// that for the 31st and February 29. ical.js mis-expands BYSETPOS, so the 29th and 30th still skip February.
static string byMonthDay(int day, int month) {
    return ";BYMONTHDAY=" + (day >= longestMonth(month) ? string("-1") : to_string(day));
}

// UNTIL this long after the last occurrence's local start still precedes the next occurrence,
// and covers a daylight-saving change between the series' start and its end.
static const time_t UNTIL_MARGIN = 2 * 60 * 60;

// Graph's patternedRecurrence as an RRULE (https://learn.microsoft.com/en-us/graph/api/resources/patternedrecurrence).
static string rruleLine(const json & recurrence, const json & start, bool allDay, time_t utcOffset) {
    const json & pattern = JSONObject(recurrence, "pattern");
    const json & range = JSONObject(recurrence, "range");
    string type = JSONString(pattern, "type");
    bool monthly = type == "absoluteMonthly" || type == "relativeMonthly";
    bool yearly = type == "absoluteYearly" || type == "relativeYearly";
    bool relative = type == "relativeMonthly" || type == "relativeYearly";

    string rule = type == "daily" ? "FREQ=DAILY" : type == "weekly" ? "FREQ=WEEKLY"
                : monthly ? "FREQ=MONTHLY" : yearly ? "FREQ=YEARLY" : "";
    if (rule == "") {
        return "";
    }
    if (JSONInt(pattern, "interval") > 1) {
        rule += ";INTERVAL=" + to_string(JSONInt(pattern, "interval"));
    }
    if (yearly) {
        rule += ";BYMONTH=" + to_string(JSONInt(pattern, "month"));
    }
    if (type == "absoluteMonthly" || type == "absoluteYearly") {
        rule += byMonthDay(JSONInt(pattern, "dayOfMonth"), yearly ? JSONInt(pattern, "month") : 0);
    }

    if (type == "weekly" || relative) {
        vector<string> days;
        for (const auto & day : JSONArray(pattern, "daysOfWeek")) {
            auto it = WEEKDAYS.find(day.is_string() ? day.get<string>() : "");
            if (it != WEEKDAYS.end()) {
                days.push_back(it->second);
            }
        }
        auto position = SET_POSITIONS.find(JSONString(pattern, "index"));
        string setPosition = position != SET_POSITIONS.end() ? position->second : "1";
        if (relative && days.size() == 1) {
            rule += ";BYDAY=" + setPosition + days.front();
        } else {
            rule += ";BYDAY=" + stringJoin(days, ",") + (relative ? ";BYSETPOS=" + setPosition : "");
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
        rule += ";COUNT=" + to_string(JSONInt(range, "numberOfOccurrences"));
    } else if (rangeType == "endDate") {
        // endDate is the inclusive last local date; a DATE-TIME UNTIL must be UTC (RFC 5545 section 3.3.10).
        time_t endDate = GraphICS::secondsAsUTC(JSONString(range, "endDate"));
        if (allDay) {
            rule += ";UNTIL=" + formatUTC(endDate, "%Y%m%d");
        } else {
            time_t timeOfDay = GraphICS::secondsAsUTC(JSONString(start, "dateTime")) % 86400;
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
    return dateTimeLine("EXDATE", formatUTC(GraphICS::secondsAsUTC(date), "%Y%m%d") + timeOfDay,
                        JSONString(start, "timeZone"), allDay);
}

static string recurrenceIdLine(const json & exception, bool allDay, time_t utcOffset) {
    time_t originalStart = secondsOfDateTimeOffset(JSONString(exception, "originalStart"));
    if (allDay) {
        // An all-day occurrence starts at local midnight: the nearest one absorbs a daylight-saving
        // change since the series' start.
        time_t local = originalStart + utcOffset;
        return "RECURRENCE-ID;VALUE=DATE:" + formatUTC((local + 43200) / 86400 * 86400, "%Y%m%d");
    }
    return "RECURRENCE-ID:" + formatUTC(originalStart, "%Y%m%dT%H%M%SZ");
}

// Graph's iCalUId is Exchange's Global Object ID in hex. For a meeting organized outside Exchange, its
// data wraps the organizer's UID as "vCal-Uid", 01 00 00 00, UID ([MS-OXCICAL] 2.1.3.1.1.20.26).
static string organizerUID(const string & iCalUId) {
    static const string BYTE_ARRAY_ID = "040000008200E00074C5B7101A82E008";
    static const string THIRD_PARTY_MARKER("vCal-Uid\x01\x00\x00\x00", 12);
    static const size_t INSTANCE_DATE_OFFSET = 16, SIZE_OFFSET = 36, DATA_OFFSET = 40;
    if (iCalUId.size() < 2 * (DATA_OFFSET + 1) || iCalUId.size() % 2 ||
        !equalsIgnoreCase(iCalUId.substr(0, BYTE_ARRAY_ID.size()), BYTE_ARRAY_ID)) {
        return iCalUId;
    }
    string bytes;
    for (size_t i = 0; i < iCalUId.size(); i += 2) {
        if (!isxdigit((unsigned char)iCalUId[i]) || !isxdigit((unsigned char)iCalUId[i + 1])) {
            return iCalUId;
        }
        bytes += (char)stoi(iCalUId.substr(i, 2), nullptr, 16);
    }
    // An orphan occurrence (an invitation to one occurrence, not its series) carries its date here.
    // Unwrapped, two of them would share one UID with no RECURRENCE-ID to tell them apart.
    if (bytes.compare(INSTANCE_DATE_OFFSET, 4, string(4, '\0')) != 0) {
        return iCalUId;
    }
    size_t size = (unsigned char)bytes[SIZE_OFFSET] | (unsigned char)bytes[SIZE_OFFSET + 1] << 8 |
                  (unsigned char)bytes[SIZE_OFFSET + 2] << 16 | (size_t)(unsigned char)bytes[SIZE_OFFSET + 3] << 24;
    string data = bytes.substr(DATA_OFFSET, size);
    if (data.compare(0, THIRD_PARTY_MARKER.size(), THIRD_PARTY_MARKER) != 0) {
        return iCalUId;
    }
    string uid = data.substr(THIRD_PARTY_MARKER.size());
    while (!uid.empty() && uid.back() == '\0') { // Graph's ends with one, counted in Size, though the spec has none
        uid.pop_back();
    }
    return uid.empty() ? iCalUId : uid;
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

static void appendVEvent(string & ics, const json & event, const string & uid, const vector<string> & mailboxAddresses,
                         const vector<string> & recurrenceLines) {
    bool allDay = JSONBool(event, "isAllDay");
    appendLine(ics, "BEGIN:VEVENT");
    appendLine(ics, "UID:" + uid);
    appendLine(ics, "DTSTAMP:" + basicDateTime(JSONString(event, "lastModifiedDateTime")) + "Z");
    appendLine(ics, dateTimeProperty("DTSTART", JSONObject(event, "start"), allDay));
    appendLine(ics, dateTimeProperty("DTEND", JSONObject(event, "end"), allDay));
    for (const auto & line : recurrenceLines) {
        appendLine(ics, line);
    }
    appendLine(ics, "SUMMARY:" + escapeText(JSONString(event, "subject")));

    string location = JSONString(JSONObject(event, "location"), "displayName");
    if (location != "") {
        appendLine(ics, "LOCATION:" + escapeText(location));
    }
    string description = JSONString(JSONObject(event, "body"), "content");
    if (description != "") {
        appendLine(ics, "DESCRIPTION:" + escapeText(description));
    }

    appendLine(ics, string("STATUS:") + (JSONBool(event, "isCancelled") ? "CANCELLED" : "CONFIRMED"));
    appendLine(ics, string("TRANSP:") + (JSONString(event, "showAs") == "free" ? "TRANSPARENT" : "OPAQUE"));

    const json & organizer = JSONObject(JSONObject(event, "organizer"), "emailAddress");
    if (JSONString(organizer, "address") != "") {
        appendLine(ics, participant("ORGANIZER", "", organizer));
    }

    // responseStatus is the mailbox's own answer; its entry in `attendees` may not reflect it.
    string myResponse = JSONString(JSONObject(event, "responseStatus"), "response");
    for (const auto & attendee : JSONArray(event, "attendees")) {
        const json & emailAddress = JSONObject(attendee, "emailAddress");
        string address = JSONString(emailAddress, "address");
        if (address == "") {
            continue;
        }
        bool mine = myResponse != "" && any_of(mailboxAddresses.begin(), mailboxAddresses.end(),
                                               [&](const string & own) { return equalsIgnoreCase(address, own); });
        string response = mine ? myResponse : JSONString(JSONObject(attendee, "status"), "response");
        string type = JSONString(attendee, "type");
        string role = type == "resource" ? ";CUTYPE=RESOURCE;ROLE=NON-PARTICIPANT"
                    : type == "optional" ? ";ROLE=OPT-PARTICIPANT"
                    : ";ROLE=REQ-PARTICIPANT";
        appendLine(ics, participant("ATTENDEE", role + ";PARTSTAT=" + partstatFor(response), emailAddress));
    }
    appendLine(ics, "END:VEVENT");
}

string GraphICS::icsForEvent(const json & event, const vector<string> & mailboxAddresses, const string & startUTC) {
    // Occurrences carry their own iCalUId; the series shares the master's.
    string uid = organizerUID(JSONString(event, "iCalUId"));
    bool allDay = JSONBool(event, "isAllDay");
    const json & start = JSONObject(event, "start");
    time_t utcOffset = GraphICS::secondsAsUTC(JSONString(start, "dateTime")) - GraphICS::secondsAsUTC(startUTC);

    vector<string> seriesLines;
    const json & recurrence = JSONObject(event, "recurrence");
    if (!recurrence.empty()) {
        string rrule = rruleLine(recurrence, start, allDay, utcOffset);
        if (rrule != "") {
            seriesLines.push_back(rrule);
        }
        for (const auto & occurrenceId : JSONArray(event, "cancelledOccurrences")) {
            if (occurrenceId.is_string()) {
                seriesLines.push_back(exdateLine(occurrenceId.get<string>(), start, allDay));
            }
        }
    }

    string ics;
    appendLine(ics, "BEGIN:VCALENDAR");
    appendLine(ics, "VERSION:2.0");
    appendLine(ics, "PRODID:-//Mailspring//Microsoft Graph//EN");
    appendVEvent(ics, event, uid, mailboxAddresses, seriesLines);
    for (const auto & exception : JSONArray(event, "exceptionOccurrences")) {
        appendVEvent(ics, exception, uid, mailboxAddresses, {recurrenceIdLine(exception, allDay, utcOffset)});
    }
    appendLine(ics, "END:VCALENDAR");
    return ics;
}

string ICSProperty::param(const string & name) const {
    auto it = params.find(name);
    return it != params.end() ? it->second : "";
}

const ICSProperty & ICSVEvent::property(const string & name) const {
    static const ICSProperty none;
    for (const auto & property : properties) {
        if (property.name == name) {
            return property;
        }
    }
    return none;
}

// RFC 5545 section 3.1: a line that begins with a space or tab continues the one before it.
static vector<string> unfoldedLines(const string & ics) {
    vector<string> lines;
    size_t start = 0;
    while (start < ics.size()) {
        size_t end = min(ics.find('\n', start), ics.size());
        string line = ics.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !lines.empty()) {
            lines.back() += line.substr(1);
        } else if (!line.empty()) {
            lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}

// The value begins at the first ':' outside a quoted parameter value (RFC 5545 section 3.2).
static ICSProperty propertyOf(const string & line) {
    ICSProperty property;
    size_t i = min(line.find_first_of(";:"), line.size());
    property.name = toUpperCase(line.substr(0, i));
    while (i < line.size() && line[i] == ';') {
        size_t nameStart = i + 1;
        i = min(line.find_first_of("=;:", nameStart), line.size());
        string name = toUpperCase(line.substr(nameStart, i - nameStart));
        string value;
        if (i < line.size() && line[i] == '=') {
            for (i++; i < line.size() && line[i] != ';' && line[i] != ':'; i++) {
                if (line[i] == '"') {
                    size_t close = min(line.find('"', i + 1), line.size());
                    value += line.substr(i + 1, close - i - 1);
                    i = close;
                } else {
                    value += line[i];
                }
            }
        }
        property.params[name] = value;
    }
    property.value = i < line.size() ? line.substr(i + 1) : "";
    return property;
}

vector<ICSVEvent> GraphICS::veventsOf(const string & ics) {
    vector<ICSVEvent> vevents;
    vector<string> open; // the components around the current line, innermost last
    for (const auto & line : unfoldedLines(ics)) {
        ICSProperty property = propertyOf(line);
        if (property.name == "BEGIN") {
            open.push_back(toUpperCase(property.value));
            if (open.back() == "VEVENT") {
                vevents.emplace_back();
            }
        } else if (property.name == "END") {
            if (!open.empty()) { // the task's ICS is whatever a client queued
                open.pop_back();
            }
        } else if (!open.empty() && open.back() == "VEVENT") {
            vevents.back().properties.push_back(property);
        }
    }
    return vevents;
}

// A DATE or DATE-TIME value (20261022, 20261022T090000Z) as Graph's 2026-10-22T09:00:00.
static string extendedDateTime(const string & basic) {
    if (basic.size() < 8) {
        return "";
    }
    string time = basic.size() >= 15 ? basic.substr(9, 2) + ":" + basic.substr(11, 2) + ":" + basic.substr(13, 2) : "00:00:00";
    return basic.substr(0, 4) + "-" + basic.substr(4, 2) + "-" + basic.substr(6, 2) + "T" + time;
}

// Graph takes an all-day event's two midnights in one zone; a VALUE=DATE has none, so UTC.
static json dateTimeTimeZone(const ICSProperty & property, bool allDay) {
    string zone = property.param("TZID");
    return {{"dateTime", extendedDateTime(property.value)}, {"timeZone", allDay || zone == "" ? "UTC" : zone}};
}

// The inverse of the roles appendVEvent writes.
static json attendeeOf(const ICSProperty & property) {
    string address = property.value;
    if (address.size() >= 7 && equalsIgnoreCase(address.substr(0, 7), "mailto:")) {
        address = address.substr(7);
    }
    string cutype = toUpperCase(property.param("CUTYPE"));
    string role = toUpperCase(property.param("ROLE"));
    string type = cutype == "RESOURCE" || cutype == "ROOM" ? "resource"
                : role == "OPT-PARTICIPANT" || role == "NON-PARTICIPANT" ? "optional"
                : "required";
    return {{"type", type}, {"emailAddress", {{"address", address}, {"name", property.param("CN")}}}};
}

// The name `names` gives `code` (WEEKDAYS: "MO" is "monday"), or "".
static string nameOf(const map<string, string> & names, const string & code) {
    for (const auto & [name, value] : names) {
        if (value == code) {
            return name;
        }
    }
    return "";
}

static int integerOf(const string & text) {
    return stringIsInteger(text) ? stringToInteger(text) : 0;
}

// UNTIL names the last occurrence by its instant (UTC) or, for an all-day series, its date; endDate
// is that occurrence's local date. `utcOffset` is the series start's: at the last occurrence
// daylight saving can make it an hour off.
static string endDateOf(const string & until, const string & start, time_t utcOffset) {
    time_t last = GraphICS::secondsAsUTC(extendedDateTime(until)) + (until.back() == 'Z' ? utcOffset : 0);
    time_t timeOfDay = GraphICS::secondsAsUTC(extendedDateTime(start)) % 86400;
    return formatUTC(last - timeOfDay + 3600, "%Y-%m-%d");
}

static const set<string> RRULE_PARTS = {"FREQ", "INTERVAL", "COUNT", "UNTIL", "WKST", "BYDAY", "BYMONTHDAY", "BYMONTH", "BYSETPOS"};

// The inverse of rruleLine, with the parts a rule may leave to DTSTART (RFC 5545 section 3.3.10):
// a weekly rule's day, a monthly rule's day of the month, a yearly rule's month and day.
static json recurrenceOf(const ICSVEvent & vevent, time_t utcOffset) {
    const string & rule = vevent.property("RRULE").value;
    const string & start = vevent.property("DTSTART").value;
    invalid_argument unsupported("RRULE:" + rule + " has no Graph recurrence pattern");

    map<string, string> parts;
    for (const auto & part : stringSplit(rule, ";")) {
        size_t equals = part.find('=');
        string name = toUpperCase(part.substr(0, equals));
        if (equals == string::npos || !RRULE_PARTS.count(name)) {
            throw unsupported;
        }
        parts[name] = toUpperCase(part.substr(equals + 1));
    }

    static const char * DAY_CODES[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    time_t startSeconds = GraphICS::secondsAsUTC(extendedDateTime(start));
    string startDay = nameOf(WEEKDAYS, DAY_CODES[integerOf(formatUTC(startSeconds, "%w"))]);

    // BYDAY=-1FR and BYDAY=MO,TU;BYSETPOS=1 both say which of the month's days.
    vector<string> days;
    string index = parts.count("BYSETPOS") ? parts["BYSETPOS"] : "";
    if (parts.count("BYDAY")) {
        vector<string> entries = stringSplit(parts["BYDAY"], ",");
        for (const auto & entry : entries) {
            string day = entry.size() >= 2 ? nameOf(WEEKDAYS, entry.substr(entry.size() - 2)) : "";
            string position = entry.substr(0, entry.size() - min<size_t>(2, entry.size()));
            if (day == "" || (position != "" && (entries.size() > 1 || index != ""))) {
                throw unsupported;
            }
            if (position != "") {
                index = position[0] == '+' ? position.substr(1) : position;
            }
            days.push_back(day);
        }
    }
    string indexName = index == "" ? "" : nameOf(SET_POSITIONS, index);
    if (index != "" && indexName == "") {
        throw unsupported;
    }

    string frequency = parts["FREQ"];
    int interval = parts.count("INTERVAL") ? integerOf(parts["INTERVAL"]) : 1;
    if (interval < 1) {
        throw unsupported;
    }
    json pattern = {{"interval", interval}};
    if (frequency == "DAILY" || frequency == "WEEKLY") {
        if (index != "" || parts.count("BYMONTHDAY") || parts.count("BYMONTH") || (frequency == "DAILY" && !days.empty())) {
            throw unsupported;
        }
        pattern["type"] = toLowerCase(frequency);
        if (frequency == "WEEKLY") {
            string weekStart = parts.count("WKST") ? nameOf(WEEKDAYS, parts["WKST"]) : "monday";
            if (weekStart == "") {
                throw unsupported;
            }
            pattern["daysOfWeek"] = days.empty() ? vector<string>{startDay} : days;
            pattern["firstDayOfWeek"] = weekStart;
        }
    } else if (frequency == "MONTHLY" || frequency == "YEARLY") {
        bool yearly = frequency == "YEARLY";
        int month = 0;
        if (yearly) {
            month = integerOf(parts.count("BYMONTH") ? parts["BYMONTH"] : formatUTC(startSeconds, "%m"));
            if (month < 1 || month > 12) {
                throw unsupported;
            }
            pattern["month"] = month;
        } else if (parts.count("BYMONTH")) {
            throw unsupported;
        }
        if (!days.empty()) {
            if (indexName == "" || parts.count("BYMONTHDAY")) {
                throw unsupported;
            }
            pattern["type"] = yearly ? "relativeYearly" : "relativeMonthly";
            pattern["daysOfWeek"] = days;
            pattern["index"] = indexName;
        } else {
            if (index != "") {
                throw unsupported;
            }
            string monthDay = parts.count("BYMONTHDAY") ? parts["BYMONTHDAY"] : formatUTC(startSeconds, "%d");
            int day = monthDay == "-1" ? longestMonth(month) : integerOf(monthDay);
            if (day < 1 || day > 31) {
                throw unsupported;
            }
            pattern["type"] = yearly ? "absoluteYearly" : "absoluteMonthly";
            pattern["dayOfMonth"] = day;
        }
    } else {
        throw unsupported;
    }

    json range = {{"startDate", formatUTC(startSeconds, "%Y-%m-%d")}};
    if (parts.count("COUNT") && parts.count("UNTIL")) {
        throw unsupported;
    }
    if (parts.count("COUNT")) {
        int count = integerOf(parts["COUNT"]);
        if (count < 1) {
            throw unsupported;
        }
        range["type"] = "numbered";
        range["numberOfOccurrences"] = count;
    } else if (parts.count("UNTIL")) {
        if (parts["UNTIL"].size() < 8) {
            throw unsupported;
        }
        range["type"] = "endDate";
        range["endDate"] = endDateOf(parts["UNTIL"], start, utcOffset);
    } else {
        range["type"] = "noEnd";
    }
    return {{"pattern", pattern}, {"range", range}};
}

json GraphICS::eventForVEvent(const ICSVEvent & vevent, const string & startUTC) {
    const ICSProperty & start = vevent.property("DTSTART");
    bool allDay = start.param("VALUE") == "DATE";
    json attendees = json::array();
    for (const auto & property : vevent.properties) {
        if (property.name == "ATTENDEE") {
            attendees.push_back(attendeeOf(property));
        }
    }
    json event = {
        {"subject", UnescapeICSText(vevent.property("SUMMARY").value)},
        {"body", {{"contentType", "text"}, {"content", UnescapeICSText(vevent.property("DESCRIPTION").value)}}},
        {"location", {{"displayName", UnescapeICSText(vevent.property("LOCATION").value)}}},
        {"isAllDay", allDay},
        {"start", dateTimeTimeZone(start, allDay)},
        {"end", dateTimeTimeZone(vevent.property("DTEND"), allDay)},
        {"showAs", vevent.property("TRANSP").value == "TRANSPARENT" ? "free" : "busy"},
        {"attendees", attendees},
    };
    if (vevent.property("RECURRENCE-ID").name.empty()) {
        time_t utcOffset = startUTC == "" || start.param("TZID") == ""
                         ? 0 : secondsAsUTC(extendedDateTime(start.value)) - secondsAsUTC(startUTC);
        event["recurrence"] = vevent.property("RRULE").name.empty() ? json() : recurrenceOf(vevent, utcOffset);
    }
    return event;
}
