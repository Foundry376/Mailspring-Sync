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
using namespace std;

class MailModel {
public:
    json _data;
    map<string, bool> _initialMetadataPluginIds;
    
    static string TABLE_NAME;
    virtual string tableName();

    MailModel(string id, string accountId, int version);
    MailModel(SQLite::Statement & query);
    MailModel(json json);
    
    void captureInitialMetadataState();
    
    string id();
    string accountId();
    int version();
    void incrementVersion();

    int upsertMetadata(string pluginId, const json & value, int version = -1);
    
    virtual void bindToQuery(SQLite::Statement * query);
    
    virtual void writeAssociations(SQLite::Database & db);
    
    virtual vector<string> columnsForQuery() = 0;

    virtual json toJSON();
    virtual json toJSONDispatch();
};

#endif /* MailModel_hpp */
