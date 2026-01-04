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

#ifndef ContactBook_hpp
#define ContactBook_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"
#include "MailStore.hpp"
#include <MailCore/MailCore.h>

using namespace nlohmann;
using namespace std;
using namespace mailcore;


class ContactBook : public MailModel {
    
public:
    static string TABLE_NAME;

    ContactBook(string id, string accountId);
    ContactBook(SQLite::Statement & query);
  
    string url();
    void setURL(string url);
    string source();
    void setSource(string source);
    string ctag();
    void setCtag(string ctag);

    string syncToken();
    void setSyncToken(string token);

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* ContactBook_hpp */
