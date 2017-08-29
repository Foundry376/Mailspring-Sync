//
//  Account.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Account_hpp
#define Account_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"

using json = nlohmann::json;
using namespace std;
using namespace mailcore;

class Account : public MailModel {
    
public:
    static string TABLE_NAME;

    Account(json json);
    Account(SQLite::Statement & query);

    bool valid();

    string xoauthRefreshToken();

    unsigned int IMAPPort();
    string IMAPHost();
    string IMAPUsername();
    string IMAPPassword();

    unsigned int SMTPPort();
    string SMTPHost();
    string SMTPUsername();
    string SMTPPassword();

    string cloudToken();
    bool hasCloudToken();

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* Account_hpp */
