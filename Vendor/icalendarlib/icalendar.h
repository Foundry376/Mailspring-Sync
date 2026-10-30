#ifndef _ICALENDAR_H
#define _ICALENDAR_H

#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <map>
#include "types.h"

using namespace std;

class ICalendar {
public:
    ICalendar(string str) { LoadFromString(str); }
	~ICalendar() {
		for_each(Events.begin(), Events.end(), DeleteItem());
	}
	void LoadFromString(string str);

	//Event* GetEventByUID(char *UID);

	void AddEvent(ICalendarEvent *NewEvent);
	void DeleteEvent(ICalendarEvent *DeletedEvent);
	void ModifyEvent(ICalendarEvent *ModifiedEvent);

	class Query;
    list<ICalendarEvent *> Events;
	map<string, shared_ptr<const TimeZone>> TimeZones; // the object's VTIMEZONEs, by TZID
	void AttachTimeZone(Date &Value) const;

private:
	// The colon that ends a content line's name and parameters. A quoted parameter value may
	// itself hold colons (RFC 5545 section 3.1), as Outlook's TZID="(UTC-05:00) Eastern..." does.
	static size_t ValueStart(const string &Line) {
		bool Quoted = false;
		for (size_t i = 0; i < Line.length(); ++i) {
			if (Line[i] == '"')
				Quoted = !Quoted;
			else if (Line[i] == ':' && !Quoted)
				return i;
		}
		return string::npos;
	}
	static string GetPropertyName(const string &Line) {
		return Line.substr(0, Line.find_first_of(";:"));
	}
	// Parameter names are case-insensitive (section 3.2).
	static bool IsParameter(const string &Params, size_t At, const string &Name) {
		if (At + Name.length() >= Params.length() || Params[At + Name.length()] != '=')
			return false;
		for (size_t i = 0; i < Name.length(); ++i) {
			if (toupper((unsigned char)Params[At + i]) != Name[i])
				return false;
		}
		return true;
	}
	// A parameter's value, unquoted, or "" when the property doesn't carry it.
	static string GetParameter(const string &Line, const string &Name) {
		const string Params = Line.substr(0, ValueStart(Line));
		bool Quoted = false;
		for (size_t i = 0; i < Params.length(); ++i) {
			if (Params[i] == '"') {
				Quoted = !Quoted;
			} else if (Params[i] == ';' && !Quoted && IsParameter(Params, i + 1, Name)) {
				size_t Start = i + Name.length() + 2;
				if (Start < Params.length() && Params[Start] == '"')
					return Params.substr(Start + 1, Params.find('"', Start + 1) - Start - 1);
				return Params.substr(Start, Params.find(';', Start) - Start);
			}
		}
		return "";
	}

	string GetProperty(const string &Line) const {
		// if VALUE=DATE or VALUE=DATE-TIME used, then the date is not directly
		// after the property name so we just search for the string after ':'
		string Temp = Line.substr(ValueStart(Line)+1);
		unsigned int Length = (unsigned int)Temp.length();
		if (Length > 0 && Temp[Length-1] == '\r')
			Temp.resize(Length-1);
		return Temp;
	}
	string GetSubProperty(const string &Line, const char *SubProperty) const {
		size_t Pos = Line.find(SubProperty);
		if (Pos == string::npos)
			return "";
		Pos += strlen(SubProperty) + 1;
		return Line.substr(Pos, Line.find_first_of(';', Pos)-Pos);
	}
	void FixLineEnd(string &Line, unsigned int Length) {
		if (Length > 0 && Line[Length-1] == '\r')
			Line.resize(Length-1);
		Line += "\r\n";
	}
	
	char *FileName;
};

class ICalendar::Query {
public:
	Query(ICalendar *Calendar): Calendar(Calendar), EventsIterator(Calendar->Events.begin()) {}
	~Query() { for_each(RecurrentEvents.begin(), RecurrentEvents.end(), DeleteItem()); }
	void ResetPosition() {
	  	for_each(RecurrentEvents.begin(), RecurrentEvents.end(), DeleteItem());
	  	RecurrentEvents.clear();
	  	EventsIterator = Calendar->Events.begin();
	}
	ICalendarEvent* GetNextEvent(bool WithAlarm = false);
	
	EventsCriteria Criteria;
	
private:
	ICalendar *Calendar;
	list<ICalendarEvent *> RecurrentEvents;
	list<ICalendarEvent *>::iterator EventsIterator;
};

inline TimeUnit ConvertFrequency(string Name) {
	if (Name == "SECONDLY")
		return SECOND;
	if (Name == "MINUTELY")
		return MINUTE;
	if (Name == "HOURLY")
		return HOUR;
	if (Name == "DAILY")
		return DAY;
	if (Name == "WEEKLY")
		return WEEK;
	if (Name == "MONTHLY")
		return MONTH;
	
	return YEAR;
}

inline AlarmAction ConvertAlarmAction(string Name) {
	if (Name == "AUDIO")
		return AUDIO;
	if (Name == "PROCEDURE")
		return PROCEDURE;
	if (Name == "EMAIL")
		return EMAIL;

	return DISPLAY;
}

// Unescape iCalendar TEXT values per RFC 5545 Section 3.3.11
// Handles: \n/\N -> newline, \\ -> backslash, \, -> comma, \; -> semicolon
inline string UnescapeICSText(const string &text) {
	string result;
	result.reserve(text.length());

	for (size_t i = 0; i < text.length(); ++i) {
		if (text[i] == '\\' && i + 1 < text.length()) {
			char next = text[i + 1];
			if (next == 'n' || next == 'N') {
				result += '\n';
				++i;
			} else if (next == '\\') {
				result += '\\';
				++i;
			} else if (next == ',') {
				result += ',';
				++i;
			} else if (next == ';') {
				result += ';';
				++i;
			} else {
				// Unknown escape sequence, keep as-is
				result += text[i];
			}
		} else {
			result += text[i];
		}
	}
	return result;
}

// Parse an ATTENDEE line per RFC 5545 Section 3.8.4.1
// Format: ATTENDEE;CN="Name";ROLE=...:mailto:email@example.com
// Returns formatted string: "Name <email>" if CN present, otherwise just email
//
// Security: ICS files come from untrusted external sources. This function:
// - Only searches for CN= in the parameter section (before colon) to prevent injection
// - Uses case-insensitive matching per RFC 5545 Section 3.1
// - Handles malformed input gracefully without throwing exceptions
inline string ParseAttendee(const string &line, const string &calAddress) {
	string email = calAddress;

	// Strip mailto: prefix if present (case-insensitive per RFC 3986)
	if (email.length() >= 7) {
		char c0 = email[0], c1 = email[1], c2 = email[2], c3 = email[3];
		char c4 = email[4], c5 = email[5], c6 = email[6];
		if ((c0 == 'm' || c0 == 'M') &&
			(c1 == 'a' || c1 == 'A') &&
			(c2 == 'i' || c2 == 'I') &&
			(c3 == 'l' || c3 == 'L') &&
			(c4 == 't' || c4 == 'T') &&
			(c5 == 'o' || c5 == 'O') &&
			c6 == ':') {
			email = email.substr(7);
		}
	}

	// Find first colon to separate parameters from value
	// CN= must appear BEFORE the colon to prevent injection via email address
	size_t colonPos = line.find(':');
	if (colonPos == string::npos) {
		// Malformed line with no colon - return email only
		return email;
	}

	// Case-insensitive search for CN= in the parameter section only (before colon)
	// RFC 5545 Section 3.1: "Parameter names...are case-insensitive"
	string cn;
	size_t cnPos = string::npos;
	for (size_t i = 0; i + 2 < colonPos; ++i) {
		if ((line[i] == 'C' || line[i] == 'c') &&
			(line[i+1] == 'N' || line[i+1] == 'n') &&
			line[i+2] == '=') {
			cnPos = i;
			break;
		}
	}

	if (cnPos != string::npos) {
		size_t valueStart = cnPos + 3;
		if (valueStart < colonPos) {
			if (line[valueStart] == '"') {
				// Quoted value - find closing quote (must be before colon)
				size_t endQuote = line.find('"', valueStart + 1);
				if (endQuote != string::npos && endQuote < colonPos) {
					cn = line.substr(valueStart + 1, endQuote - valueStart - 1);
				}
			} else {
				// Unquoted value - ends at ; or : (whichever comes first)
				size_t endPos = line.find_first_of(";:", valueStart);
				if (endPos != string::npos && endPos <= colonPos) {
					cn = line.substr(valueStart, endPos - valueStart);
				}
			}
		}
	}

	// Format as "Name <email>" if CN present, otherwise just email
	if (!cn.empty()) {
		return cn + " <" + email + ">";
	}
	return email;
}

#endif // _ICALENDAR_H
