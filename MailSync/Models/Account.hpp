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

#ifndef Account_hpp
#define Account_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"

using namespace nlohmann;
using namespace std;
using namespace mailcore;

class Account : public MailModel {
    
public:
    static string TABLE_NAME;

    Account(json json);
    Account(SQLite::Statement & query);

    int startDelay();
    
    string valid();

    string refreshToken();
    void setRefreshToken(string value);

    string refreshClientId();
    string provider();
    string emailAddress();

    unsigned int IMAPPort();
    string IMAPHost();
    string IMAPUsername();
    string IMAPPassword();
    string IMAPSecurity();
    bool IMAPAllowInsecureSSL();

    bool isICloud();

    unsigned int SMTPPort();
    string SMTPHost();
    string SMTPUsername();
    string SMTPPassword();
    string SMTPSecurity();
    bool SMTPAllowInsecureSSL();

    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    
    string containerFolder();
};

#endif /* Account_hpp */
