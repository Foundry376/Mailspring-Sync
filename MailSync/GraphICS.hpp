//
//  GraphICS.hpp
//  MailSync
//
//  Copyright © 2026 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef GraphICS_hpp
#define GraphICS_hpp

#include <ctime>
#include <string>
#include <vector>
#include "json.hpp"

using namespace nlohmann;
using namespace std;

// Microsoft Graph events as the iCalendar resources Event rows hold; Graph has no iCalendar form.
class GraphICS {
public:
    // `event` fetched in its own zone (Prefer: outlook.timezone) with a text body; a series master
    // also with $select=cancelledOccurrences and $expand=exceptionOccurrences. `mailboxAddresses`:
    // the user's own, whose attendee entry gets the mailbox's answer. `startUTC`: the event's start
    // as Graph lists it in UTC, which gives a series its UTC offset.
    static string icsForEvent(const json & event, const vector<string> & mailboxAddresses, const string & startUTC);

    // A Graph date-time or date ("2026-10-22T09:00:00.0000000", "2026-12-31") read as UTC.
    static time_t secondsAsUTC(const string & extended);
};

#endif /* GraphICS_hpp */
