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

#include <string>
#include "json.hpp"

using namespace nlohmann;
using namespace std;

// Microsoft Graph events as the iCalendar resources Event rows hold; Graph has no iCalendar form.
class GraphICS {
public:
    // `event` fetched in its own zone (Prefer: outlook.timezone) with a text body; a series master
    // also with $select=cancelledOccurrences and $expand=exceptionOccurrences. `startUTC`: the
    // event's start as Graph lists it in UTC, which gives a series its UTC offset.
    static string icsForEvent(const json & event, const string & mailboxAddress, const string & startUTC);
};

#endif /* GraphICS_hpp */
