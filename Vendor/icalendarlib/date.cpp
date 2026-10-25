#include "date.h"
#include <algorithm>
#include <climits>
#include <map>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#include <unicode/ucal.h>
#include <unicode/ustring.h>
#endif

string Date::Format() const {
	char Temp[16];
	sprintf(Temp, "%.4d/%.2d/%.2d", Data[YEAR], Data[MONTH], Data[DAY]);
	if (WithTime == true)
		sprintf(Temp+10, " %.2d:%.2d", Data[HOUR], Data[MINUTE]);
	return string(Temp);
}

Date::operator string() const {
	char Temp[16];
	sprintf(Temp, "%.4d%.2d%.2d", Data[YEAR], Data[MONTH], Data[DAY]);
	if (WithTime == true)
		sprintf(Temp+8, "T%.2d%.2d%.2d", Data[HOUR], Data[MINUTE], Data[SECOND]);
	return string(Temp);
}

Date &Date::operator =(const string &Text) {
	if (Text.length() >= 8) {
		sscanf(Text.c_str(), "%4hd%2hd%2hd", &Data[YEAR], &Data[MONTH], &Data[DAY]);
		
		if (Text.length() >= 15) {
			sscanf(Text.c_str()+9, "%2hd%2hd%2hd", &Data[HOUR], &Data[MINUTE], &Data[SECOND]);
			WithTime = true;
			// A trailing "Z" makes the value UTC (RFC 5545 section 3.3.5); toUnix() needs it.
			// Scan back over trailing whitespace: GetSubProperty() hands over RRULE parts such
			// as UNTIL with the line's carriage return still attached, unlike GetProperty().
			size_t last = Text.find_last_not_of(" \t\r\n");
			IsUTC = (last != string::npos && (Text[last] == 'Z' || Text[last] == 'z'));
		} else {
			Data[HOUR] = Data[MINUTE] = Data[SECOND] = 0;
			WithTime = false;
			IsUTC = false;
		}
	} else {
		Data[YEAR] = Data[MONTH] = Data[DAY] = 0;
		IsUTC = false;
	}
	return *this;
}

// difference *this - OtherDate
unsigned long Date::Difference(Date &OtherDate, TimeUnit Unit, bool RoundUp) const {
	unsigned long Result = 0;
	
	switch (Unit) {
		case YEAR:
			Result = Data[YEAR] - OtherDate.Data[YEAR];
			break;
			
		case MONTH:
			Result = (Data[YEAR] - OtherDate.Data[YEAR])*12 + Data[MONTH] - OtherDate.Data[MONTH];
			break;
			
		case DAY:
			if (OtherDate.Data[YEAR] == Data[YEAR]) {
				if (OtherDate.Data[MONTH] == Data[MONTH]) {
					Result = Data[DAY] - OtherDate.Data[DAY];
				} else {
					Result = (OtherDate.DaysInMonth() - OtherDate.Data[DAY]) + Data[DAY];
					
					for (char i=OtherDate.Data[MONTH]+1; i<Data[MONTH]; ++i)
						Result += OtherDate.DaysInMonth(i);
				}
			} else {
				// number of days in the starting year
				Result = OtherDate.DaysInMonth() - OtherDate.Data[DAY];
				for (char i=OtherDate.Data[MONTH]+1; i<=12; ++i)
					Result += OtherDate.DaysInMonth(i);
				
				// number of days in years between the starting and ending years
				for (short i=OtherDate.Data[YEAR]+1; i<Data[YEAR]; ++i)
					Result += IsLeapYear(i) ? 366 : 365;
				
				// number of days in the ending year
				for (char i=1; i<Data[MONTH]; ++i)
					Result += DaysInMonth(i);
				Result += Data[DAY];
			}
			
			break;
			
		case HOUR:
			if (Data[DAY] == OtherDate.Data[DAY]) {
				Result = Data[HOUR] - OtherDate.Data[HOUR];
			} else {
				Result = 24 - OtherDate.Data[HOUR];
				Result += (Difference(OtherDate, DAY, false) - 1) * 24;
				Result += Data[HOUR];
			}
			break;
			
		case MINUTE:
			if (Data[HOUR] == OtherDate.Data[HOUR]) {
				Result = Data[MINUTE] - OtherDate.Data[MINUTE];
			} else {
				Result = 60 - OtherDate.Data[MINUTE];
				Result += (Difference(OtherDate, HOUR, false) - 1) * 60;
				Result += Data[MINUTE];
			}
			break;
			
		case SECOND:
			if (Data[MINUTE] == OtherDate.Data[MINUTE]) {
				Result = Data[SECOND] - OtherDate.Data[SECOND];
			} else {
				Result = 60 - OtherDate.Data[SECOND];
				Result += (Difference(OtherDate, MINUTE, false) - 1) * 60;
				Result += Data[SECOND];
			}
			break;
			
		case WEEK:
			Result = Difference(OtherDate, DAY, false)/7 + 1;
			break;
	}
	
	if (RoundUp == true && Unit < SECOND && Compare(OtherDate, (TimeUnit)(Unit+1)) > 0)
		++Result;
	return Result;
}

void Date::SetToNow() {
	time_t Timestamp;
	tm *CurrentTime;
	
	time(&Timestamp);
	CurrentTime = localtime(&Timestamp);
	
	Data[YEAR] = CurrentTime->tm_year + 1900;
	Data[MONTH] = CurrentTime->tm_mon + 1;
	Data[DAY] = CurrentTime->tm_mday;
	Data[HOUR] = CurrentTime->tm_hour;
	Data[MINUTE] = CurrentTime->tm_min;
	Data[SECOND] = CurrentTime->tm_sec;
	
	WithTime = true;
	IsUTC = false; // localtime() above gives a local wall-clock, not a UTC one
}

Date::DatePart &Date::DatePart::operator +=(short Value) {
	switch (Part) {
		case YEAR:
			BaseDate.Data[YEAR] += Value;
			break;
		
		case MONTH:
			BaseDate.Data[MONTH] += Value;
			BaseDate[YEAR] += (BaseDate.Data[MONTH]-1)/12;
			BaseDate.Data[MONTH] = (BaseDate.Data[MONTH]-1)%12 + 1;
			break;
		
		case DAY:
			BaseDate.Data[DAY] += Value;
			while (BaseDate.Data[DAY] > BaseDate.DaysInMonth()) {
				BaseDate.Data[DAY] -= BaseDate.DaysInMonth();
				BaseDate[MONTH] += 1;
			}
			break;
		
		case HOUR:
			BaseDate.Data[HOUR] += Value;
			BaseDate[DAY] += BaseDate.Data[HOUR]/24;
			BaseDate.Data[HOUR] %= 24;
			break;
		
		case MINUTE:
			BaseDate.Data[MINUTE] += Value;
			BaseDate[HOUR] += BaseDate.Data[MINUTE]/60;
			BaseDate.Data[MINUTE] %= 60;
			break;
		
		case SECOND:
			BaseDate.Data[SECOND] += Value;
			BaseDate[MINUTE] += BaseDate.Data[SECOND]/60;
			BaseDate.Data[SECOND] %= 60;
			break;
		
		case WEEK:
			BaseDate[DAY] += Value*7;
			break;
	}
	return *this;
}

Date::DatePart &Date::DatePart::operator -=(short Value) {
	switch (Part) {
		case YEAR:
			BaseDate.Data[YEAR] -= Value;
			break;
		
		case MONTH:
			BaseDate.Data[MONTH] -= Value;
			if (BaseDate.Data[MONTH] < 1) {
				BaseDate[YEAR] -= -(BaseDate.Data[MONTH]-12)/12;
				BaseDate.Data[MONTH] = 12 + BaseDate.Data[MONTH]%12;
			}
			break;
		
		case DAY:
			BaseDate.Data[DAY] -= Value;
			while (BaseDate.Data[DAY] < 1) {
				BaseDate[MONTH] -= 1;
				BaseDate.Data[DAY] += BaseDate.DaysInMonth();
			}
			break;
		
		case HOUR:
			BaseDate.Data[HOUR] -= Value;
			if (BaseDate.Data[HOUR] < 0) {
				BaseDate[DAY] -= -(BaseDate.Data[HOUR]-24)/24;
				BaseDate.Data[HOUR] = 24 + BaseDate.Data[HOUR]%24;
			}
			break;
		
		case MINUTE:
			BaseDate.Data[MINUTE] -= Value;
			if (BaseDate.Data[MINUTE] < 0) {
				BaseDate[HOUR] -= -(BaseDate.Data[MINUTE]-60)/60;
				BaseDate.Data[MINUTE] = 60 + BaseDate.Data[MINUTE]%60;
			}
			break;
		
		case SECOND:
			BaseDate.Data[SECOND] -= Value;
			if (BaseDate.Data[SECOND] < 0) {
				BaseDate[MINUTE] -= -(BaseDate.Data[SECOND]-60)/60;
				BaseDate.Data[SECOND] = 60 + BaseDate.Data[SECOND]%60;
			}
			break;
		
		case WEEK:
			BaseDate[DAY] -= Value*7;
			break;
	}
	return *this;
}

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's days_from_civil).
static int64_t DaysFromCivil(int64_t Year, unsigned Month, unsigned Day) {
	Year -= Month <= 2;
	const int64_t Era = (Year >= 0 ? Year : Year - 399) / 400;
	const unsigned YearOfEra = (unsigned)(Year - Era * 400);
	const unsigned DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
	const unsigned DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
	return Era * 146097 + (int64_t)DayOfEra - 719468;
}

// The proleptic Gregorian year holding a count of seconds since 1970 (civil_from_days). It only
// bounds how far the zone's changes are enumerated, so a day truncated towards 1970 is harmless.
static int YearOf(int64_t Seconds) {
	const int64_t Days = Seconds / 86400 + 719468;
	const int64_t Era = (Days >= 0 ? Days : Days - 146096) / 146097;
	const unsigned DayOfEra = (unsigned)(Days - Era * 146097);
	const unsigned YearOfEra = (DayOfEra - DayOfEra / 1460 + DayOfEra / 36524 - DayOfEra / 146096) / 365;
	const unsigned DayOfYear = DayOfEra - (365 * YearOfEra + YearOfEra / 4 - YearOfEra / 100);
	const unsigned MonthIndex = (5 * DayOfYear + 2) / 153;
	return (int)(YearOfEra + Era * 400 + (MonthIndex >= 10));
}

static int64_t CivilSeconds(int Year, int Month, int Day, int Hour, int Minute, int Second) {
	return DaysFromCivil(Year, Month, Day) * 86400 + Hour * 3600 + Minute * 60 + Second;
}

// 0 = Sunday, as BYDAY's SU..SA are numbered below; 1970-01-01 was a Thursday.
static int Weekday(int Year, int Month, int Day) {
	return (int)(((DaysFromCivil(Year, Month, Day) + 4) % 7 + 7) % 7);
}

static int DaysIn(int Year, int Month) {
	Date D;
	D[YEAR] = Year;
	return D.DaysInMonth(Month);
}

int64_t Date::WallClockSeconds() const {
	return CivilSeconds(Data[YEAR], Data[MONTH], Data[DAY], Data[HOUR], Data[MINUTE], Data[SECOND]);
}

static vector<string> Split(const string &Text, char Separator) {
	vector<string> Parts;
	size_t Start = 0;
	for (;;) {
		size_t End = Text.find(Separator, Start);
		Parts.push_back(Text.substr(Start, End - Start));
		if (End == string::npos)
			return Parts;
		Start = End + 1;
	}
}

namespace {
/*
 The parts of an observance's RRULE that VTIMEZONEs use: a yearly rule on a month's nth or last
 weekday, a weekday within a run of month days, or a fixed day. A rule using any other part makes
 the whole zone unreadable rather than guessed at.
*/
struct YearlyRule {
	int Interval = 1, Count = 0;
	Date Until;
	vector<int> Months, MonthDays, SetPositions;
	vector<pair<int, int>> Days; // (ordinal, weekday); ordinal 0 = every such weekday

	bool Parse(const string &Text) {
		static const char *Names[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
		bool Yearly = false;
		for (const string &Part : Split(Text, ';')) {
			size_t Equals = Part.find('=');
			if (Equals == string::npos)
				return false;
			const string Key = Part.substr(0, Equals), Value = Part.substr(Equals + 1);
			if (Key == "FREQ") {
				Yearly = Value == "YEARLY";
			} else if (Key == "INTERVAL") {
				Interval = atoi(Value.c_str());
			} else if (Key == "COUNT") {
				Count = atoi(Value.c_str());
			} else if (Key == "UNTIL") {
				Until = Value;
			} else if (Key == "BYMONTH") {
				for (const string &V : Split(Value, ','))
					Months.push_back(atoi(V.c_str()));
			} else if (Key == "BYMONTHDAY") {
				for (const string &V : Split(Value, ','))
					MonthDays.push_back(atoi(V.c_str()));
			} else if (Key == "BYSETPOS") {
				for (const string &V : Split(Value, ','))
					SetPositions.push_back(atoi(V.c_str()));
			} else if (Key == "BYDAY") {
				for (const string &V : Split(Value, ',')) {
					if (V.length() < 2)
						return false;
					const string Name = V.substr(V.length() - 2);
					const int WeekdayIndex = (int)(find(Names, Names + 7, Name) - Names);
					if (WeekdayIndex == 7)
						return false;
					Days.push_back({atoi(V.substr(0, V.length() - 2).c_str()), WeekdayIndex});
				}
			} else if (Key != "WKST") {
				return false;
			}
		}
		// Without BYMONTH a BYDAY or BYMONTHDAY counts within the year (section 3.3.10), a
		// shape no VTIMEZONE writes; it is refused rather than read within DTSTART's month.
		return Yearly && Interval > 0 && (!Months.empty() || (Days.empty() && MonthDays.empty()));
	}

	// The wall-clock onsets the rule gives in a year, ascending, at DTSTART's time of day.
	vector<int64_t> OnsetsIn(int Year, const vector<int> &InMonths, Date &Start) const {
		vector<int64_t> Onsets;
		for (int Month : InMonths) {
			for (int Day : DaysOf(Year, Month, Start[DAY]))
				Onsets.push_back(CivilSeconds(Year, Month, Day, Start[HOUR], Start[MINUTE], Start[SECOND]));
		}
		sort(Onsets.begin(), Onsets.end());
		if (SetPositions.empty())
			return Onsets;
		vector<int64_t> Selected;
		for (int Position : SetPositions) {
			const int Index = Position > 0 ? Position - 1 : (int)Onsets.size() + Position;
			if (Index >= 0 && Index < (int)Onsets.size())
				Selected.push_back(Onsets[Index]);
		}
		sort(Selected.begin(), Selected.end());
		return Selected;
	}

	// The days the rule gives in one month of a year, ascending.
	vector<int> DaysOf(int Year, int Month, int StartDay) const {
		const int Length = DaysIn(Year, Month);
		const int FirstWeekday = Weekday(Year, Month, 1);
		vector<int> Result;
		for (int Day = 1; Day <= Length; ++Day) {
			bool Matches;
			if (!MonthDays.empty()) {
				Matches = any_of(MonthDays.begin(), MonthDays.end(), [&](int M) {
					return M == Day || M == Day - Length - 1;
				});
			} else {
				Matches = Day == StartDay;
			}
			if (!Days.empty()) {
				const int W = (FirstWeekday + Day - 1) % 7;
				const bool DayMatches = any_of(Days.begin(), Days.end(), [&](const pair<int, int> &D) {
					if (D.second != W)
						return false;
					// An ordinal counts within the month, from its end when negative.
					if (D.first == 0)
						return true;
					return D.first > 0 ? (Day - 1) / 7 + 1 == D.first : (Length - Day) / 7 + 1 == -D.first;
				});
				Matches = (MonthDays.empty() || Matches) && DayMatches;
			}
			if (Matches)
				Result.push_back(Day);
		}
		return Result;
	}
};
}

static bool ParseOffset(const string &Text, int &Seconds) {
	int Hours = 0, Minutes = 0, Secs = 0;
	if (Text.length() < 5 || (Text[0] != '+' && Text[0] != '-'))
		return false;
	if (sscanf(Text.c_str() + 1, "%2d%2d%2d", &Hours, &Minutes, &Secs) < 2)
		return false;
	Seconds = (Text[0] == '-' ? -1 : 1) * (Hours * 3600 + Minutes * 60 + Secs);
	return true;
}

bool VTimeZone::ChangesThrough(int LastYear, vector<Change> &Changes) const {
	for (const TimeZoneObservance &Observance : Observances) {
		if (!AddChanges(Observance, LastYear, Changes))
			return false;
	}
	sort(Changes.begin(), Changes.end(), [](const Change &A, const Change &B) { return A.At < B.At; });
	return true;
}

bool VTimeZone::AddChanges(const TimeZoneObservance &Observance, int LastYear, vector<Change> &Changes) const {
	int From, To;
	Date Start;
	Start = Observance.DtStart;
	if (!ParseOffset(Observance.OffsetFrom, From) || !ParseOffset(Observance.OffsetTo, To) || Start.IsEmpty())
		return false;
	const auto Add = [&](int64_t Local) { Changes.push_back({Local - From, From, To}); };
	const int64_t First = Start.WallClockSeconds();
	Add(First);
	for (const string &Text : Observance.RDates) {
		Date RDate;
		RDate = Text;
		if (RDate.IsEmpty())
			return false;
		Add(RDate.WallClockSeconds());
	}
	if (Observance.RRule.empty())
		return true;

	YearlyRule Rule;
	if (!Rule.Parse(Observance.RRule))
		return false;
	// UNTIL is UTC in a VTIMEZONE (section 3.6.5); a DATE runs to the end of its day.
	const int64_t Until = Rule.Until.IsEmpty() ? INT64_MAX
		: Rule.Until.WallClockSeconds() + (Rule.Until.WithTime ? 0 : 86399);
	const vector<int> Months = Rule.Months.empty() ? vector<int>{Start[MONTH]} : Rule.Months;
	int Seen = 1; // DTSTART is the first onset
	for (int Year = Start[YEAR]; Year <= LastYear; Year += Rule.Interval) {
		for (int64_t Local : Rule.OnsetsIn(Year, Months, Start)) {
			if (Local <= First)
				continue;
			if ((Rule.Until.IsUTC ? Local - From : Local) > Until || (Rule.Count > 0 && ++Seen > Rule.Count))
				return true;
			Add(Local);
		}
	}
	return true;
}

bool VTimeZone::OffsetForWallClock(int64_t Wall, int &Offset) const {
	vector<Change> Changes;
	if (!ChangesThrough(YearOf(Wall), Changes) || Changes.empty())
		return false;

	// The offset in force before the first change is the one that change leaves. Where two
	// offsets both give this wall clock, the earlier instant is taken (section 3.3.5).
	for (size_t i = 0; i <= Changes.size(); ++i) {
		const int Candidate = i == 0 ? Changes[0].From : Changes[i - 1].To;
		const int64_t Utc = Wall - Candidate;
		if ((i == 0 || Utc >= Changes[i - 1].At) && (i == Changes.size() || Utc < Changes[i].At)) {
			Offset = Candidate;
			return true;
		}
	}
	// A wall clock a change skips over reads in the offset before the gap (section 3.3.5).
	for (const Change &Gap : Changes) {
		if (Wall >= Gap.At + Gap.From && Wall < Gap.At + Gap.To) {
			Offset = Gap.From;
			return true;
		}
	}
	return false;
}

#ifdef __APPLE__

static void *OpenSystemZone(const string &TzId) {
	CFStringRef Name = CFStringCreateWithCString(kCFAllocatorDefault, TzId.c_str(), kCFStringEncodingUTF8);
	if (!Name)
		return nullptr;
	CFTimeZoneRef Zone = CFTimeZoneCreateWithName(kCFAllocatorDefault, Name, true);
	CFRelease(Name);
	return (void *)Zone; // CFTimeZoneRef is a const pointer; the untyped handle is shared with ICU's
}

SystemTimeZone::~SystemTimeZone() {
	CFRelease((CFTimeZoneRef)Handle);
}

bool SystemTimeZone::OffsetAt(int64_t Utc, int &Offset) const {
	Offset = (int)CFTimeZoneGetSecondsFromGMT((CFTimeZoneRef)Handle, (CFAbsoluteTime)Utc - kCFAbsoluteTimeIntervalSince1970);
	return true;
}

#else

// ICU opens an unknown name as the "Etc/Unknown" zone instead of failing, so the opened zone's
// own id tells whether the name was known. A Windows name ("Eastern Standard Time") is mapped
// to its IANA zone through CLDR's table.
static void *OpenSystemZone(const string &TzId) {
	UErrorCode Status = U_ZERO_ERROR;
	UChar Name[128], Id[128];
	char IdUtf8[256];
	int32_t NameLength = 0, IdLength = 0;
	u_strFromUTF8(Name, 128, &NameLength, TzId.c_str(), (int32_t)TzId.length(), &Status);
	if (U_FAILURE(Status))
		return nullptr;
	UCalendar *Calendar = ucal_open(Name, NameLength, "en_US", UCAL_GREGORIAN, &Status);
	if (U_FAILURE(Status))
		return nullptr;
	IdLength = ucal_getTimeZoneID(Calendar, Id, 128, &Status);
	u_strToUTF8(IdUtf8, 256, nullptr, Id, IdLength, &Status);
	if (U_SUCCESS(Status) && strcmp(IdUtf8, UCAL_UNKNOWN_ZONE_ID) != 0)
		return Calendar;
	ucal_close(Calendar);
	Status = U_ZERO_ERROR;
	IdLength = ucal_getTimeZoneIDForWindowsID(Name, NameLength, nullptr, Id, 128, &Status);
	if (U_FAILURE(Status) || IdLength == 0)
		return nullptr;
	Calendar = ucal_open(Id, IdLength, "en_US", UCAL_GREGORIAN, &Status);
	return U_SUCCESS(Status) ? Calendar : nullptr;
}

SystemTimeZone::~SystemTimeZone() {
	ucal_close((UCalendar *)Handle);
}

bool SystemTimeZone::OffsetAt(int64_t Utc, int &Offset) const {
	UErrorCode Status = U_ZERO_ERROR;
	lock_guard<mutex> Guard(Lock);
	UCalendar *Calendar = (UCalendar *)Handle;
	ucal_setMillis(Calendar, (UDate)Utc * 1000.0, &Status);
	const int32_t Zone = ucal_get(Calendar, UCAL_ZONE_OFFSET, &Status);
	const int32_t Daylight = ucal_get(Calendar, UCAL_DST_OFFSET, &Status);
	if (U_FAILURE(Status))
		return false;
	Offset = (Zone + Daylight) / 1000;
	return true;
}

#endif

shared_ptr<const TimeZone> SystemTimeZone::Named(const string &TzId) {
	static map<string, shared_ptr<const TimeZone>> Zones;
	static mutex ZonesLock;
	lock_guard<mutex> Guard(ZonesLock);
	auto Found = Zones.find(TzId);
	if (Found != Zones.end())
		return Found->second;
	void *Handle = OpenSystemZone(TzId);
	if (!Handle)
		return nullptr;
	return Zones[TzId] = shared_ptr<const TimeZone>(new SystemTimeZone(Handle));
}

// The system gives the offset at an instant; the instant a wall clock means is the one whose
// offset gives that wall clock back. A day either side of it brackets any change in between
// (changes are months apart). The larger offset is tried first: it is the earlier instant, the
// first occurrence of a wall clock a change repeats (section 3.3.5).
bool SystemTimeZone::OffsetForWallClock(int64_t Wall, int &Offset) const {
	int Before, After, Actual;
	if (!OffsetAt(Wall - 86400, Before) || !OffsetAt(Wall + 86400, After))
		return false;
	for (int Candidate : {max(Before, After), min(Before, After)}) {
		if (OffsetAt(Wall - Candidate, Actual) && Actual == Candidate) {
			Offset = Candidate;
			return true;
		}
	}
	// A wall clock a change skips over reads in the offset before the gap (section 3.3.5).
	Offset = Before;
	return true;
}
