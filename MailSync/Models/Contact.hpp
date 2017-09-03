//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Contact_hpp
#define Contact_hpp

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

class Message;

class Contact : public MailModel {
    
public:
    static string TABLE_NAME;

    Contact(string accountId, string canonicalizedEmail, json json);
    Contact(SQLite::Statement & query);
  
    string email();
    string searchContent();
    
    int refs();
    void incrementRefs();
                          
    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Contact_hpp */
