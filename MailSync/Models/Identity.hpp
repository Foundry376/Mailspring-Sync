//
//  Account.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef Identity_hpp
#define Identity_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"

using namespace nlohmann;
using namespace std;

class Identity : public MailModel {

public:
    static shared_ptr<Identity> _global;
    static shared_ptr<Identity> GetGlobal();
    static void SetGlobal(shared_ptr<Identity> i);
    
    static string TABLE_NAME;

    Identity(json json);

    bool valid();

    time_t createdAt();
    string firstName();
    string lastName();
    string emailAddress();
    string token();
    
    
    string tableName();
    string constructorName();
    
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Identity_hpp */
