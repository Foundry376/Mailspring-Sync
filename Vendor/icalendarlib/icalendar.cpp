#include "icalendar.h"
#include <sstream>

void ICalendar::LoadFromString(string icsData) {
	string Line, NextLine;
	Component CurrentComponent = VCALENDAR, PrevComponent = VCALENDAR;
	ICalendarEvent *NewEvent = NULL;
	Alarm NewAlarm;
	// for getting some UIDs for events without them
	unsigned int NoUID = 0;

    istringstream File;
    File.str(icsData);

	getline(File, NextLine);

	while (!File.eof()) {
		Line = NextLine;
		// lines can be wrapped after 75 octets so we may have to unwrap them
		for (;;) {
			getline(File, NextLine);
			if (NextLine[0] != '\t' && NextLine[0] != ' ')
				break;
			Line += NextLine.substr(1);
		}

		switch (CurrentComponent) {
			case VCALENDAR:
				if (Line.find("BEGIN:VEVENT") == 0) {
					NewEvent = new ICalendarEvent;
					CurrentComponent = VEVENT;
				}
				break;

			case VEVENT:
				if (Line.find("UID") == 0) {
					NewEvent->UID = GetProperty(Line);
				} else if (Line.find("SUMMARY") == 0) {
					NewEvent->Summary = GetProperty(Line);
				} else if (Line.find("DTSTAMP") == 0) {
					NewEvent->DtStamp = GetProperty(Line);
				} else if (Line.find("DTSTART") == 0) {
					NewEvent->DtStart = GetProperty(Line);
				} else if (Line.find("DTEND") == 0) {
					NewEvent->DtEnd = GetProperty(Line);
				} else if (Line.find("DESCRIPTION") == 0) {
					NewEvent->Description = GetProperty(Line);
				} else if (Line.find("CATEGORIES") == 0) {
					NewEvent->Categories = GetProperty(Line);
				} else if (Line.find("RRULE") == 0) {
					NewEvent->RRule.Freq = ConvertFrequency(GetSubProperty(Line, "FREQ"));
					NewEvent->RRule.Interval = atoi(GetSubProperty(Line, "INTERVAL").c_str());
					if (NewEvent->RRule.Interval == 0)
						NewEvent->RRule.Interval = 1;
					NewEvent->RRule.Count = atoi(GetSubProperty(Line, "COUNT").c_str());
					NewEvent->RRule.Until = GetSubProperty(Line, "UNTIL");
				} else if (Line.find("RECURRENCE-ID") == 0) {
					// RECURRENCE-ID identifies which occurrence of a recurring event is being modified
					// Format: RECURRENCE-ID:20240115T100000Z or RECURRENCE-ID;TZID=...:20240115T100000
					NewEvent->RecurrenceId = GetProperty(Line);
				} else if (Line.find("STATUS") == 0) {
					// STATUS can be TENTATIVE, CONFIRMED, or CANCELLED
					NewEvent->Status = GetProperty(Line);
				} else if (Line.find("LOCATION") == 0) {
					// LOCATION is a TEXT value that may contain escaped characters
					NewEvent->Location = UnescapeICSText(GetProperty(Line));
				} else if (Line.find("ATTENDEE") == 0) {
					NewEvent->Attendees.push_back(ParseAttendee(Line, GetProperty(Line)));
				} else if (Line.find("BEGIN:VALARM") == 0) {
					NewAlarm.Clear();
					PrevComponent = CurrentComponent;
					CurrentComponent = VALARM;
				} else if (Line.find("END:VEVENT") == 0) {
					if (NewEvent->UID.empty())
						NewEvent->UID = NoUID++;

					Events.push_back(NewEvent);
					CurrentComponent = VCALENDAR;
				}
				break;

			case VALARM:
				if (Line.find("ACTION") == 0) {
					NewAlarm.Action = ConvertAlarmAction(GetProperty(Line));
				} else if (Line.find("TRIGGER") == 0) {
					NewAlarm.Trigger = GetProperty(Line);
				} else if (Line.find("DESCRIPTION") == 0) {
					NewAlarm.Description = GetProperty(Line);
				} else if (Line.find("END:VALARM") == 0) {
					NewEvent->Alarms->push_back(NewAlarm);
					CurrentComponent = PrevComponent;
				}
				break;
		}
	}
}

/*Event* ICalendar::GetEventByUID(char *UID) {
	for (list<Event *>::iterator Iterator = Events.begin(); Iterator != Events.end(); ++Iterator) {
		if ((*Iterator)->UID.find(UID) == 0) {
			return *Iterator;
		}
	}

	return NULL;
}*/

void ICalendar::AddEvent(ICalendarEvent *NewEvent) {
	char Temp[16];
	string Line;
	streamoff Offset;

	NewEvent->DtStamp.SetToNow();
	NewEvent->UID = NewEvent->DtStamp;
	NewEvent->UID += '-';
	sprintf(Temp, "%d", rand());
	NewEvent->UID += Temp;

	Events.push_back(NewEvent);

	// for some reason tellg() modifies the get pointer under Windows if the file
	// is not opened in the binary mode (possibly because of UTF-8?)
	fstream File(FileName, ios::in | ios::out | ios::binary);

	do {
		Offset = File.tellg();
		getline(File, Line);
	} while (!File.eof() && Line.find("END:VCALENDAR") != 0);
	File.seekp(Offset, ios::beg);

	File << *NewEvent;
	File << "END:VCALENDAR\r\n";

	File.close();
}

void ICalendar::DeleteEvent(ICalendarEvent *DeletedEvent) {
	fstream File;
	string Data, Line, PartialData;
	unsigned int Length;
	bool Copy = true, Deleted = false;

	File.open(FileName, ios::in | ios::binary);
	File.seekg(0, ios::end);
	Length = (unsigned int)File.tellg();
	File.seekg(0, ios::beg);

	// to avoid reallocating memory
	Data.reserve(Length);

	while (!File.eof()) {
		getline(File, Line);

		Length = (unsigned int)Line.length();
		if (Length <= 1)
			continue;

		// getline() removes only '\n' from the end of the line (not '\r')
		FixLineEnd(Line, Length);

		if (Line.find("BEGIN:VEVENT") == 0) {
			Copy = false;
			Deleted = false;
			PartialData = "";
		} else if (Line.find("UID:") == 0 && Line.find(DeletedEvent->UID) == 4) {
			Deleted = true;
		}

		if (Copy == true)
			Data += Line;
		else if (Deleted == false)
			PartialData += Line;

		if (Line.find("END:VEVENT") == 0) {
			Copy = true;

			if (Deleted == false)
				Data += PartialData;
		}
	}

	File.close();
	File.clear();

	// again problems in non-binary mode - "\r\n" changed to "\r\r\n"
	File.open(FileName, ios::out | ios::binary | ios::trunc);
	File << Data;
	File.close();

	for (list<ICalendarEvent *>::iterator Iterator = Events.begin(); Iterator != Events.end();) {
		if (*Iterator == DeletedEvent) {
			delete *Iterator;
			Events.erase(Iterator++);
		} else
			++Iterator;
	}
}

void ICalendar::ModifyEvent(ICalendarEvent *ModifiedEvent) {
	fstream File;
	string Data, Line, PartialData;
	unsigned int Length;
	bool Copy = true, Modified = false;

	File.open(FileName, ios::in | ios::binary);
	File.seekg(0, ios::end);
	Length = (unsigned int)File.tellg();
	File.seekg(0, ios::beg);

	// we will probably need at least such amount of memory
	Data.reserve(Length);

	while (!File.eof()) {
		getline(File, Line);

		Length = (unsigned int)Line.length();
		if (Length <= 1)
			continue;

		// getline() removes only '\n' from the end of the line (not '\r')
		FixLineEnd(Line, Length);

		if (Line.find("BEGIN:VEVENT") == 0) {
			Copy = false;
			Modified = false;
			PartialData = "";
		} else if (Line.find("UID:") == 0 && Line.find(ModifiedEvent->UID) == 4) {
			Modified = true;
			Data += *ModifiedEvent;
		}

		if (Copy == true)
			Data += Line;
		else if (Modified == false)
			PartialData += Line;

		if (Line.find("END:VEVENT") == 0) {
			Copy = true;

			if (Modified == false)
				Data += PartialData;
		}
	}

	File.close();
	File.clear();

	// again problems in non-binary mode - "\r\n" changed to "\r\r\n"
	File.open(FileName, ios::out | ios::binary | ios::trunc);
	File << Data;
	File.close();
}

/// ICalendar::Query

ICalendarEvent* ICalendar::Query::GetNextEvent(bool WithAlarm) {
	static ICalendarEvent *RecurrentEvent = NULL;
	/* not all events have DtEnd, but we need some DtEnd for various checks,
	   so we will use this for temporary DtEnd derived from DtStart (following
	   RFC 2445, 4.6.1) */
	Date DtEnd;
	unsigned long Difference;
	unsigned short Rest;

	if (RecurrentEvent != NULL) {
		RecurrentEvent->DtStart[RecurrentEvent->RRule.Freq] += RecurrentEvent->RRule.Interval;
		if (!RecurrentEvent->DtEnd.IsEmpty())
			RecurrentEvent->DtEnd[RecurrentEvent->RRule.Freq] += RecurrentEvent->RRule.Interval;
		++RecurrentEvent->RecurrenceNo;

		if (
		(!WithAlarm &&
		RecurrentEvent->DtStart <= Criteria.To &&
		(RecurrentEvent->RRule.Until.IsEmpty() || RecurrentEvent->RRule.Until >= RecurrentEvent->DtStart) &&
		(RecurrentEvent->RRule.Count == 0 || RecurrentEvent->RecurrenceNo < RecurrentEvent->RRule.Count)) ||
		(WithAlarm && RecurrentEvent->HasAlarm(Criteria.From, Criteria.To))
		) {
			RecurrentEvents.push_back(new ICalendarEvent(*RecurrentEvent));
			return RecurrentEvents.back();
		}

		delete RecurrentEvent;
		RecurrentEvent = NULL;
	}

	if (RecurrentEvent == NULL) {
		for (; EventsIterator != Calendar->Events.end(); ++EventsIterator) {
			if ((*EventsIterator)->DtEnd.IsEmpty()) {
				DtEnd = (*EventsIterator)->DtStart;
				if ((*EventsIterator)->DtStart.WithTime == false)
					++DtEnd[DAY];
			} else {
				DtEnd = (*EventsIterator)->DtEnd;
			}

			if (
			Criteria.AllEvents == true || (
			!WithAlarm &&
			// DtEnd is non-inclusive (according to RFC 2445)
			(DtEnd > Criteria.From || (*EventsIterator)->DtStart >= Criteria.From) &&
			(*EventsIterator)->DtStart <= Criteria.To
			) ||
			(WithAlarm && (*EventsIterator)->HasAlarm(Criteria.From, Criteria.To))
			) {
				if (Criteria.AllEvents == false && Criteria.IncludeRecurrent == true && (*EventsIterator)->RRule.IsEmpty() == false)
					RecurrentEvent = new ICalendarEvent(**EventsIterator);
				return *(EventsIterator++);

			} else if (
			(*EventsIterator)->RRule.IsEmpty() == false &&
			(*EventsIterator)->DtStart < Criteria.From &&
			((*EventsIterator)->RRule.Until.IsEmpty() || (*EventsIterator)->RRule.Until >= Criteria.From) &&
			Criteria.IncludeRecurrent == true
			) {
				RecurrentEvent = new ICalendarEvent(**EventsIterator);

				Difference = Criteria.From.Difference(DtEnd, RecurrentEvent->RRule.Freq);
				Rest = Difference%RecurrentEvent->RRule.Interval;

				if (Rest != 0)
					Difference += RecurrentEvent->RRule.Interval - Rest;
				
//                cout << Criteria.From.Format() << endl;
//                cout << DtEnd.Format() << endl;
//                cout << Difference << endl;
				RecurrentEvent->DtStart[RecurrentEvent->RRule.Freq] += Difference;
				DtEnd[RecurrentEvent->RRule.Freq] += Difference;
				RecurrentEvent->RecurrenceNo = Difference / RecurrentEvent->RRule.Interval;

				// <= because DtEnd is non-inclusive (according to RFC 2445)
				while (DtEnd <= Criteria.From) {
					RecurrentEvent->DtStart[RecurrentEvent->RRule.Freq] += RecurrentEvent->RRule.Interval;
					DtEnd[RecurrentEvent->RRule.Freq] += RecurrentEvent->RRule.Interval;
					++RecurrentEvent->RecurrenceNo;
				}

				if (
				(!WithAlarm &&
				RecurrentEvent->DtStart <= Criteria.To &&
				// < because DtStart counts as the first occurence
				(RecurrentEvent->RRule.Count == 0 || RecurrentEvent->RecurrenceNo < RecurrentEvent->RRule.Count)) ||
				(WithAlarm && RecurrentEvent->HasAlarm(Criteria.From, Criteria.To))
				) {
					++EventsIterator;
					if (!RecurrentEvent->DtEnd.IsEmpty())
						RecurrentEvent->DtEnd = DtEnd;
					RecurrentEvents.push_back(new ICalendarEvent(*RecurrentEvent));
					return RecurrentEvents.back();
				}

				delete RecurrentEvent;
				RecurrentEvent = NULL;
			}
		}
	}

	return NULL;
}
