//
//  MailModel.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef MailModel_hpp
#define MailModel_hpp

#include <stdio.h>
#include <vector>
#include <string>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "json.hpp"

using json = nlohmann::json;

class MailModel {
protected:
    std::string _id;
    std::string _accountId;
    std::string _data;
    int _version;
    
public:
    static std::string TABLE_NAME;
    virtual std::string tableName();

    MailModel(std::string id, std::string accountId, int version);
    MailModel(SQLite::Statement & query);
    
    std::string id();
    std::string accountId();
    int version();
    void incrementVersion();

    virtual void bindToQuery(SQLite::Statement & query);

    virtual std::vector<std::string> columnsForQuery() = 0;

    virtual json toJSON();
};

#endif /* MailModel_hpp */
