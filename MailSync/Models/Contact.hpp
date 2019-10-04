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

#define CONTACT_SOURCE_MAIL "mail"
#define CONTACT_MAX_REFS 100000

class Message;

class Contact : public MailModel {
    
public:
    static string TABLE_NAME;

    Contact(string accountId, string canonicalizedEmail, json json, string source);
    Contact(SQLite::Statement & query);
  
    string name();
    string email();
    bool hidden();
    void setHidden(bool b);
    string source();
    string searchContent();
    json info();
    void setInfo(json info);
    string etag();
    string remoteCollectionId();

    int refs();
    void incrementRefs();
                          
    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
    
    void afterSave(MailStore * store);
    void afterRemove(MailStore * store);
};

#endif /* Contact_hpp */
