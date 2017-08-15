//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Task_hpp
#define Task_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"

using json = nlohmann::json;
using namespace std;
using namespace mailcore;

class Task : public MailModel {
    
public:
    static string TABLE_NAME;

    Task(string constructorName, string accountId, json taskSpecificData);
    
    Task(json json);
    Task(SQLite::Statement & query);
  
    json & data();
    string tableName();
    string constructorName();

    string status();
    void setStatus(string s);
    
    bool shouldCancel();
    void setShouldCancel();

    json error();
    void setError(json e);

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Task_hpp */
