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

#ifndef ContactGroup_hpp
#define ContactGroup_hpp

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


class ContactGroup : public MailModel {
    
public:
    static string TABLE_NAME;

    ContactGroup(string id, string accountId);
    ContactGroup(SQLite::Statement & query);
  
    string name();
    void setName(string name);
    string bookId();
    void setBookId(string bookId);
    string googleResourceName();
    void setGoogleResourceName(string rn);
                          
    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    
    void afterRemove(MailStore * store);
    
    vector<string> getMembers(MailStore * store);
    void syncMembers(MailStore * store, vector<string> newContactIds);
};

#endif /* ContactGroup_hpp */
