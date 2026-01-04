//
//  Calendar.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Calendar_hpp
#define Calendar_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"

#include "MailModel.hpp"

using namespace nlohmann;
using namespace std;

class Calendar : public MailModel {
    
public:
    static string TABLE_NAME;

    Calendar(json & json);
    Calendar(string id, string accountId);
    Calendar(SQLite::Statement & query);
    
    string path();
    void setPath(string path);

    string name();
    void setName(string name);

    string ctag();
    void setCtag(string ctag);

    string syncToken();
    void setSyncToken(string token);

    // Calendar metadata (synced from CalDAV server)
    string color();
    void setColor(string color);

    string description();
    void setDescription(string description);

    bool readOnly();
    void setReadOnly(bool readOnly);

    int order();
    void setOrder(int order);

    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Calendar_hpp */
