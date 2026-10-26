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

static string dateTimeProperty(const string & name, const json & dateTimeTimeZone, bool allDay) {
    string value = basicDateTime(JSONString(dateTimeTimeZone, "dateTime"));
    if (allDay) {
        return name + ";VALUE=DATE:" + value.substr(0, 8);
    }
    string zone = JSONString(dateTimeTimeZone, "timeZone");
    if (zone == "UTC") {
        return name + ":" + value + "Z";
    }
    return name + ";TZID=" + paramValue(zone) + ":" + value;
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

static void appendVEvent(string & ics, const json & event, const string & mailboxAddress) {
    bool allDay = flag(event, "isAllDay");
    appendLine(ics, "BEGIN:VEVENT");
    appendLine(ics, "UID:" + JSONString(event, "iCalUId"));
    appendLine(ics, "DTSTAMP:" + basicDateTime(JSONString(event, "lastModifiedDateTime")) + "Z");
    appendLine(ics, dateTimeProperty("DTSTART", child(event, "start"), allDay));
    appendLine(ics, dateTimeProperty("DTEND", child(event, "end"), allDay));
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

string GraphICS::icsForEvent(const json & event, const string & mailboxAddress) {
    string ics;
    appendLine(ics, "BEGIN:VCALENDAR");
    appendLine(ics, "VERSION:2.0");
    appendLine(ics, "PRODID:-//Mailspring//Microsoft Graph//EN");
    appendVEvent(ics, event, mailboxAddress);
    appendLine(ics, "END:VCALENDAR");
    return ics;
}
