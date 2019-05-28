//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Event_hpp
#define Event_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "icalendar.h"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"
#include "MailStore.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class Event : public MailModel {
    
public:
    static string TABLE_NAME;

Event(string etag, string accountId, string calendarId, string ics, ICalendarEvent * event);
    Event(SQLite::Statement & query);
    
    string etag();
    string calendarId();
    string icsData();
    string icsUID();
    
    int recurrenceStart();
    int recurrenceEnd();

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Event_hpp */
