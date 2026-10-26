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

// Microsoft Graph events as the iCalendar resources CalDAV serves, which is what Event rows
// hold and the client reads. Graph has no iCalendar form of an event.
class GraphICS {
public:
    // `event` as fetched in its own time zone (Prefer: outlook.timezone) with a text body.
    // The attendee at `mailboxAddress` answers with the event's responseStatus.
    static string icsForEvent(const json & event, const string & mailboxAddress);
};

#endif /* GraphICS_hpp */
