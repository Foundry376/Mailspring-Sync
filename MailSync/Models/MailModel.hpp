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

class MailModel {
protected:
    std::string _id;
    int _version;
    
public:
    MailModel(std::string id, int version);
    MailModel(SQLite::Statement & query);
    
    std::string id();
    int getVersion();
    void incrementVersion();

    virtual std::string tableName() = 0;
    virtual std::vector<std::string> columnsForQuery() = 0;
    virtual void bindToQuery(SQLite::Statement & query) = 0;
};

#endif /* MailModel_hpp */
