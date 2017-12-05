//
//  MailModel.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MailModel_hpp
#define MailModel_hpp

#include <stdio.h>
#include <vector>
#include <string>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "json.hpp"

using namespace std;
using namespace nlohmann;

class MailStore;

class MailModel {
public:
    json _data;

    map<string, int> _initialMetadataPluginIds;
    
    static string TABLE_NAME;
    virtual string tableName();

    MailModel(string id, string accountId, int version = 0);
    MailModel(SQLite::Statement & query);
    MailModel(json json);
    
    void captureInitialMetadataState();
    
    string id();
    string accountId();
    int version();
    void incrementVersion();

    int upsertMetadata(string pluginId, const json & value, int version = -1);
    json & metadata();

    virtual bool supportsMetadata();

    virtual void bindToQuery(SQLite::Statement * query);
    
    virtual void beforeSave(MailStore * store);
    virtual void afterSave(MailStore * store);
    virtual void afterRemove(MailStore * store);
    
    virtual vector<string> columnsForQuery() = 0;

    virtual json toJSON();
    virtual json toJSONDispatch();
};

#endif /* MailModel_hpp */
