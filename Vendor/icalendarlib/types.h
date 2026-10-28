#ifndef _TYPES_H
#define _TYPES_H

#include <string>
#include <list>
#include "date.h"

using namespace std;

struct DeleteItem {
	template <typename T>
	void operator()(T *ptr) { delete ptr; }
};

// Custom data types

typedef enum { VCALENDAR, VEVENT, VALARM, VTIMEZONE, TZOBSERVANCE } Component;
typedef enum { DISPLAY=0, PROCEDURE, AUDIO, EMAIL } AlarmAction;

struct Recurrence {
	Recurrence(): Freq(YEAR), Interval(0), Count(0) {}
	operator string() const;
	bool IsEmpty() const { return (Interval == 0); }
	void Clear() { Interval = 0; }
	
	TimeUnit Freq;
	unsigned short Interval, Count;
	Date Until;
};

struct AlarmTrigger {
	AlarmTrigger(): Before(true), Value(0), Unit(MINUTE) {}
	AlarmTrigger &operator =(const string &Text);
	operator string() const;
	
	bool Before;
	unsigned short Value;
	TimeUnit Unit;
};

struct Alarm {
	Alarm(): Active(false), Action(DISPLAY) {}
	operator string() const;
	void Clear() {
		Description.clear();
	}
	
	bool Active;
	AlarmAction Action;
	AlarmTrigger Trigger;
	string Description;
};

/*
 Reads a DURATION value (RFC 5545 section 3.3.6) into its nominal days (weeks and days) and exact
 seconds (the time part). False for a value outside that grammar, a negative one included: an
 event's DURATION must be positive (section 3.8.2.5).
*/
bool ParseDuration(const string &Value, long long &Days, long long &Seconds);

struct ICalendarEvent {
	ICalendarEvent(): Alarms(new list<Alarm>), RecurrenceNo(0), BaseEvent(this) {}
	ICalendarEvent(const ICalendarEvent &Base):
		UID(Base.UID),
		Summary(Base.Summary),
		Description(Base.Description),
		Categories(Base.Categories),
		RecurrenceId(Base.RecurrenceId),
		RecurrenceIdTzId(Base.RecurrenceIdTzId),
		Status(Base.Status),
		Location(Base.Location),
		Organizer(Base.Organizer),
		Attendees(Base.Attendees),
		DtStamp(Base.DtStamp),
		DtStart(Base.DtStart),
		DtEnd(Base.DtEnd),
		Duration(Base.Duration),
		RRule(Base.RRule),
		Alarms(Base.Alarms),
		RecurrenceNo(Base.RecurrenceNo)
	{
		BaseEvent = Base.BaseEvent == (ICalendarEvent *)&Base ? (ICalendarEvent *)&Base : Base.BaseEvent;
	}
	~ICalendarEvent() {
		if (BaseEvent == this)
			delete Alarms;
	}
	operator string() const;
	bool HasAlarm(const Date &From, const Date &To);
	/*
	 The instant the event ends (RFC 5545 section 3.6.1): DTEND, else DTSTART plus DURATION, else
	 the next day after a DATE start and the start itself after a DATE-TIME start. The weeks and
	 days of a DURATION are nominal, so a day across a daylight-saving change is 23 or 25 hours;
	 its time part is exact. An end past the start of 2038 is reported as that instant.
	*/
	time_t EndUnix() const;

	string UID, Summary, Description, Categories;
	string RecurrenceId;  // RECURRENCE-ID for exception instances (e.g., "20240115T100000Z")
	string RecurrenceIdTzId;  // its TZID parameter; empty for a UTC or floating value
	string Status;        // STATUS: TENTATIVE, CONFIRMED, CANCELLED
	string Location;      // LOCATION: physical or virtual location
	string Organizer;     // ORGANIZER: event organizer (as "Name <email>" or just email) - RFC 5546 MUST for REPLY
	list<string> Attendees;  // ATTENDEE: list of participants (each as "Name <email>" or just email)
	Date DtStamp, DtStart, DtEnd;
	/** The DURATION property's value as written (RFC 5545 section 3.3.6); empty without one. */
	string Duration;
	Recurrence RRule;
	list<Alarm> *Alarms;
	unsigned short RecurrenceNo;
	ICalendarEvent *BaseEvent;
};

struct EventsCriteria {
	EventsCriteria(): AllEvents(false), IncludeRecurrent(true) {}
	
	Date From, To;
	bool AllEvents, IncludeRecurrent;
};

inline ostream &operator <<(ostream &stream, const Recurrence &RRule) {
	stream << RRule.operator string();
	return stream;
}

inline ostream &operator <<(ostream &stream, const Alarm &Alarm) {
	stream << Alarm.operator string();
	return stream;
}

inline ostream &operator <<(ostream &stream, const ICalendarEvent &ICalendarEvent) {
	stream << ICalendarEvent.operator string();
	return stream;
}

#endif // _TYPES_H
