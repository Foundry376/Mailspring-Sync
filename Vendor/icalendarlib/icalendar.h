#ifndef _ICALENDAR_H
#define _ICALENDAR_H

#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstring>
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

private:
	string GetProperty(const string &Line) const {
		// if VALUE=DATE or VALUE=DATE-TIME used, then the date is not directly
		// after the property name so we just search for the string after ':'
		string Temp = Line.substr(Line.find_first_of(':')+1);
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
inline string ParseAttendee(const string &line, const string &calAddress) {
	string email = calAddress;

	// Strip mailto: prefix if present (case-insensitive)
	if (email.length() >= 7) {
		string prefix = email.substr(0, 7);
		if (prefix == "mailto:" || prefix == "MAILTO:") {
			email = email.substr(7);
		}
	}

	// Extract CN (common name) parameter if present
	// CN can be quoted: CN="John Smith" or unquoted: CN=John
	string cn;
	size_t cnPos = line.find("CN=");
	if (cnPos != string::npos) {
		size_t valueStart = cnPos + 3;
		if (valueStart < line.length()) {
			if (line[valueStart] == '"') {
				// Quoted value - find closing quote
				size_t endQuote = line.find('"', valueStart + 1);
				if (endQuote != string::npos) {
					cn = line.substr(valueStart + 1, endQuote - valueStart - 1);
				}
			} else {
				// Unquoted value - ends at ; or :
				size_t endPos = line.find_first_of(";:", valueStart);
				if (endPos != string::npos) {
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
