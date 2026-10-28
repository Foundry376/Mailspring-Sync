#ifndef _DATE_H
#define _DATE_H

#include <string>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <time.h>
#include <iomanip>
#include <sstream>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>

using namespace std;

typedef enum { YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, WEEK } TimeUnit;

/** A STANDARD or DAYLIGHT sub-component of a VTIMEZONE, as written. */
struct TimeZoneObservance {
	string DtStart, RRule, OffsetFrom, OffsetTo;
	vector<string> RDates;
};

/*
 The rules of a named zone. A wall clock that two offsets both give (a clock set back) is read
 in the first, and one that no offset gives (a clock set forward) in the offset in force before
 the gap (RFC 5545 section 3.3.5).
*/
class TimeZone {
public:
	virtual ~TimeZone() {}
	/** The offset a wall-clock time is read in, or false when the rules can't be evaluated. */
	virtual bool OffsetForWallClock(int64_t Wall, int &Offset) const = 0;
};

/*
 A VTIMEZONE (RFC 5545 section 3.6.5). Each observance's offset takes effect at its DTSTART and
 at every RRULE and RDATE onset, all wall-clock times read in the offset being left.
*/
class VTimeZone : public TimeZone {
public:
	vector<TimeZoneObservance> Observances;

	bool OffsetForWallClock(int64_t Wall, int &Offset) const override;

private:
	struct Change { int64_t At; int From, To; }; // At is UTC seconds
	bool ChangesThrough(int LastYear, vector<Change> &Changes) const;
	bool AddChanges(const TimeZoneObservance &Observance, int LastYear, vector<Change> &Changes) const;
};

/*
 A zone in ICU's time zone database: the ICU linked on Linux and Windows, the system's libicucore
 on macOS, so a name resolves to the same zone everywhere. Each reads the tzdata shipped with the
 engine when it is installed (UseZoneFilesIn), which is why a zone found here outranks the
 VTIMEZONE an object carries (see ICalendar::AttachTimeZone).
*/
class SystemTimeZone : public TimeZone {
public:
	/** The zone of an IANA name, or of a Windows name through CLDR's table; null when ICU has no
	    such zone. A name is looked up once, found or not, and its zone shared. */
	static shared_ptr<const TimeZone> Named(const string &TzId);
	/** Reads zones from the ICU zone files (zoneinfo64.res and its companions) in Directory.
	    Takes effect only before the first zone is opened. */
	static void UseZoneFilesIn(const string &Directory);
	/** The tzdata release zones are read from, e.g. "2026e"; empty if ICU can't say. */
	static string DataVersion();
	~SystemTimeZone();

	bool OffsetForWallClock(int64_t Wall, int &Offset) const override;

private:
	explicit SystemTimeZone(void *Handle) : Handle(Handle) {}
	bool OffsetAt(int64_t Utc, int &Offset) const;

	void *Handle; // a UCalendar; ucal.h is not in the macOS SDK, so this header can't name it
	mutable mutex Lock; // a UCalendar is positioned before it is read, so one reader at a time
};

class Date {
private:
	// An int, not a char: char is unsigned on Linux arm64, where -1 would compare as 255.
	int Compare(const Date &OtherDate, TimeUnit FromPart = YEAR) const {
		for (;FromPart <= SECOND; FromPart = (TimeUnit)(FromPart+1)) {
			if (Data[FromPart] < OtherDate.Data[FromPart])
				return -1;
			else if (Data[FromPart] > OtherDate.Data[FromPart])
				return 1;
		}
		return 0;
	};
	
	short Data[6];
	class DatePart;
	
public:
	Date() {
		Clear();
	}
	
    /*
     An iCalendar DATE-TIME is one of three things (RFC 5545 section 3.3.5): UTC when it ends
     in "Z", a wall-clock time in a named zone when the property carries TZID, or floating.
     A zoned time is read through the zone ICalendar::AttachTimeZone found for its TZID. A
     floating time, a DATE, and a TZID no zone was found for resolve against the local zone;
     tm_isdst is -1 so the C library determines daylight saving.
    */
    int toUnix() {
        int Offset;
        if (WithTime && !IsUTC && Zone && Zone->OffsetForWallClock(WallClockSeconds(), Offset)) {
            return (int)(WallClockSeconds() - Offset);
        }
        struct tm timeinfo {};
        memset(&timeinfo, 0, sizeof(struct tm));
        char Temp[16];
        sprintf(Temp, "%.4d%.2d%.2dT%.2d%.2d%.2d", Data[YEAR], Data[MONTH], Data[DAY], Data[HOUR], Data[MINUTE], Data[SECOND]);
        std::istringstream ss(Temp);
        ss >> std::get_time(&timeinfo, "%Y%m%dT%H%M%S");
        timeinfo.tm_isdst = -1; // let the C library work out DST for local times
        if (IsUTC) {
#ifdef _MSC_VER
            return (int)_mkgmtime(&timeinfo);
#else
            return (int)timegm(&timeinfo);
#endif
        }
        return (int)mktime(&timeinfo);
    }

    bool IsLeapYear(short Year) const {
		return ((Year%4 == 0 && Year%100 != 0) || Year%400 == 0);
	}
	
	short DaysInMonth(short Month = 0) const {
		switch (Month == 0 ? Data[MONTH] : Month) {
			case 1: return 31;
			case 2: return IsLeapYear(Data[YEAR]) ? 29 : 28;
			case 3: return 31;
			case 4: return 30;
			case 5: return 31;
			case 6: return 30;
			case 7: return 31;
			case 8: return 31;
			case 9: return 30;
			case 10: return 31;
			case 11: return 30;
			case 12: return 31;
		}
		return 0;
	}
	
	string Format() const;
	operator string() const;
	Date &operator =(const string &Text);
	friend ostream &operator <<(ostream &stream, const Date &Date) {
		stream << Date.operator string();
		return stream;
	}
	bool operator <=(const Date &OtherDate) const { return (Compare(OtherDate) <= 0); }
	bool operator >=(const Date &OtherDate) const { return (Compare(OtherDate) >= 0); }
	bool operator <(const Date &OtherDate) const { return (Compare(OtherDate) < 0); }
	bool operator >(const Date &OtherDate) const { return (Compare(OtherDate) > 0); }
	bool operator ==(const Date &OtherDate) const { return (Compare(OtherDate) == 0); }
	DatePart operator [](TimeUnit PartName);

	/** The fields as seconds since 1970-01-01T00:00:00, ignoring any zone. */
	int64_t WallClockSeconds() const;
	
	unsigned long Difference(Date &OtherDate, TimeUnit Unit, bool RoundUp = true) const;
	void SetToNow();
	bool IsEmpty() const { return (Data[YEAR] == 0); }
	void Clear(bool OnlyTime = false) {
		if (!OnlyTime)
			Data[YEAR] = Data[MONTH] = Data[DAY] = 0;
		
		Data[HOUR] = Data[MINUTE] = Data[SECOND] = 0;
		WithTime = false;
		IsUTC = false;
		TzId.clear();
		Zone.reset();
	}
	
	bool WithTime;
	/** True when the value was written in UTC, i.e. ended in "Z". */
	bool IsUTC;
	/** The property's TZID parameter (empty for a UTC, floating or DATE value), and the zone
	    ICalendar::AttachTimeZone found for it. */
	string TzId;
	shared_ptr<const TimeZone> Zone;
};

class Date::DatePart {
private:
	Date &BaseDate;
	TimeUnit Part;
	
public:
	DatePart(Date &BaseDate, TimeUnit Part): BaseDate(BaseDate), Part(Part) {}
	
	operator short() const {
		if (Part <= SECOND)
			return BaseDate.Data[Part];
		else if (Part == WEEK) {
			// actually, this case is unused
			short Value = 0;
			for (char i=1; i<BaseDate.Data[MONTH]; ++i)
				Value += BaseDate.DaysInMonth(i);
			Value += BaseDate.Data[DAY];
			
			return (Value-1)/7 + 1;
		}

		return 0;
	}
	
	DatePart &operator =(short Value) {
		if (Part <= SECOND)
			BaseDate.Data[Part] = Value;
		
		return *this;
	}
	
	DatePart &operator =(const DatePart &DatePart) {
		return operator =((short)DatePart);
	}
	
	DatePart &operator +=(short Value);
	DatePart &operator -=(short Value);
	DatePart &operator ++() {
		operator +=(1);
		return *this;
	}
};

inline Date::DatePart Date::operator [](TimeUnit Part) {
	return DatePart(*this, Part);
}

#endif // _DATE_H
