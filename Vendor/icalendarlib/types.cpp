#include "types.h"

#include <cctype>
#include <cstdlib>

Recurrence::operator string() const {
	char Temp[5];
	string Text = "FREQ=";
	switch (Freq) {
		case YEAR: Text += "YEARLY"; break;
		case MONTH: Text += "MONTHLY"; break;
		case DAY: Text += "DAILY"; break;
		case HOUR: Text += "HOURLY"; break;
		case MINUTE: Text += "MINUTELY"; break;
		case SECOND: Text += "SECONDLY"; break;
		case WEEK: Text += "WEEKLY"; break;
	}
	Text += ";INTERVAL=";
	sprintf(Temp, "%d", Interval);
	Text += Temp;
	if (Count > 0) {
		Text += ";COUNT=";
		sprintf(Temp, "%d", Count);
		Text += Temp;
	} else if (!Until.IsEmpty()) {
		Text += ";UNTIL=";
		Text += Until;
	}
	
	return Text;
}

AlarmTrigger &AlarmTrigger::operator =(const string &Text) {
	char UnitChar;
	// 1 because always at least 'P' before the value
	short i = 1;
	
	if (Text[0] == '-')
		Before = true;
	
	while (!isdigit(Text[i]))
		++i;
	
	sscanf(Text.c_str()+i, "%hd%c", (short *)&Value, &UnitChar);
	
	switch (UnitChar) {
		case 'H': Unit = HOUR; break;
		case 'D': Unit = DAY; break;
		case 'W': Unit = WEEK; break;
		default: Unit = MINUTE; break;
	}
	
	return *this;
}

AlarmTrigger::operator string() const {
	string Text;
	char Temp[6];
	
	sprintf(Temp, "%d", Value);
	
	if (Before)
		Text = '-';
	Text += 'P';
	if (Unit != WEEK && Unit != DAY)
		Text += 'T';
	Text += Temp;
	switch (Unit) {
		case HOUR: Text += 'H'; break;
		case DAY: Text += 'D'; break;
		case WEEK: Text += 'W'; break;
		default: Text += 'M'; break;
	}
	
	return Text;
}

Alarm::operator string() const {
	string Text = "\r\nBEGIN:VALARM";
	Text += "\r\nTRIGGER:";
	Text += Trigger;
	Text += "\r\nACTION:";
	switch (Action) {
		case AUDIO: Text += "AUDIO"; break;
		case DISPLAY: Text += "DISPLAY"; break;
		case PROCEDURE: Text += "PROCEDURE"; break;
		case EMAIL: Text += "EMAIL"; break;
	}
	if (!Description.empty()) {
		Text += "\r\nDESCRIPTION:";
		Text += Description;
	}
	Text += "\r\nEND:VALARM";
	
	return Text;
}

bool ParseDuration(const string &Value, long long &Days, long long &Seconds) {
	static const string Units = "WDHMS";
	Days = Seconds = 0;
	size_t i = 0;
	if (i < Value.size() && Value[i] == '+')
		i++;
	if (i >= Value.size() || Value[i] != 'P')
		return false;
	i++;
	bool InTime = false;
	int LastUnit = -1;
	while (i < Value.size()) {
		// A week count stands alone, and each unit may appear once, in descending order.
		if (LastUnit == 0)
			return false;
		if (Value[i] == 'T') {
			if (InTime)
				return false;
			InTime = true;
			i++;
			continue;
		}
		size_t Digits = i;
		while (i < Value.size() && isdigit((unsigned char)Value[i]))
			i++;
		if (i == Digits || i - Digits > 9)
			return false;
		long long Count = atoll(Value.substr(Digits, i - Digits).c_str());
		// At the end of the value this is its terminating null, which no unit matches.
		char Unit = Value[i++];
		size_t Rank = Units.find(Unit);
		if (Rank == string::npos || (Rank >= 2) != InTime || (LastUnit >= 0 && Rank <= (size_t)LastUnit))
			return false;
		LastUnit = (int)Rank;
		switch (Unit) {
			case 'W': Days += 7 * Count; break;
			case 'D': Days += Count; break;
			case 'H': Seconds += 3600 * Count; break;
			case 'M': Seconds += 60 * Count; break;
			case 'S': Seconds += Count; break;
		}
	}
	// "P" and "PT" name no unit; "P1DT" opens a time part and gives none.
	return LastUnit >= 0 && (!InTime || LastUnit >= 2);
}

time_t ICalendarEvent::EndUnix() const {
	Date End = DtEnd;
	if (!End.IsEmpty())
		return End.toUnix();
	Date Start = DtStart;
	long long Days = 0, Seconds = 0;
	if (!ParseDuration(Duration, Days, Seconds)) {
		Days = Start.WithTime ? 0 : 1;
		Seconds = 0;
	}
	// DatePart arithmetic takes a short; a span of years goes in steps.
	while (Days > 0) {
		short Step = Days > 10000 ? 10000 : (short)Days;
		Start[DAY] += Step;
		Days -= Step;
	}
	return Start.toUnix() + Seconds;
}

ICalendarEvent::operator string() const {
	string Text = "BEGIN:VEVENT";
	Text += "\r\nUID:";
	Text += UID;
	Text += "\r\nSUMMARY:";
	
	Text += Summary;
	// some programs do not add this property, let's leave it when modifying
	if (!DtStamp.IsEmpty()) {
		Text += "\r\nDTSTAMP:";
		Text += DtStamp;
	}
	Text += "\r\nDTSTART:";
	Text += DtStart;
	if (!DtEnd.IsEmpty()) {
		Text += "\r\nDTEND:";
		Text += DtEnd;
	} else if (!Duration.empty()) {
		Text += "\r\nDURATION:";
		Text += Duration;
	}
	if (!Description.empty()) {
		Text += "\r\nDESCRIPTION:";
		Text += Description;
	}
	if (!Categories.empty()) {
		Text += "\r\nCATEGORIES:";
		Text += Categories;
	}
	if (!RRule.IsEmpty()) {
		Text += "\r\nRRULE:";
		Text += RRule;
	}
	
	if (!Alarms->empty()) {
		for (list<Alarm>::const_iterator AlarmsIterator = Alarms->begin(); AlarmsIterator != Alarms->end(); ++AlarmsIterator) {
			Text += *AlarmsIterator;
		}
	}
	
	Text += "\r\nEND:VEVENT\r\n";
	
	return Text;
}

bool ICalendarEvent::HasAlarm(const Date &From, const Date &To) {
	if (Alarms->empty())
		return false;
	
	bool HasAlarm = false;
	Date AlarmDate;
	
	for (list<Alarm>::iterator AlarmsIterator = Alarms->begin(); AlarmsIterator != Alarms->end(); ++AlarmsIterator) {
		AlarmDate = DtStart;
		
		if ((*AlarmsIterator).Trigger.Before)
			AlarmDate[(*AlarmsIterator).Trigger.Unit] -= (*AlarmsIterator).Trigger.Value;
		else
			AlarmDate[(*AlarmsIterator).Trigger.Unit] += (*AlarmsIterator).Trigger.Value;
		
		if (AlarmDate >= From && AlarmDate <= To) {
			HasAlarm = true;
			(*AlarmsIterator).Active = true;
		} else {
			(*AlarmsIterator).Active = false;
		}
	}
	
	return HasAlarm;
}
