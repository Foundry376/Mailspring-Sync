#ifndef _ICALENDAR_WXWRAPPER_H
#define _ICALENDAR_WXWRAPPER_H

#include <wx/datetime.h>

inline Date &operator <<(Date &Date, const wxDateTime &wxDate) {
	Date[YEAR] = wxDate.GetYear();
	Date[MONTH] = wxDate.GetMonth()+1;
	Date[DAY] = wxDate.GetDay();
	Date[HOUR] = 0;
	Date[MINUTE] = 0;
	Date[SECOND] = 0;
	Date.WithTime = true;

	return Date;
}

inline wxDateTime CreateWxDateTime(Date &Date) {
	return wxDateTime(Date[DAY], wxDateTime::Month(wxDateTime::Jan + Date[MONTH] - 1), Date[YEAR]);
}

#endif // _ICALENDAR_WXWRAPPER_H
