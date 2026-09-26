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

#ifndef Folder_hpp
#define Folder_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"

#include "MailModel.hpp"

using namespace nlohmann;
using namespace std;

class Folder : public MailModel {
    
public:
    static string TABLE_NAME;

    Folder(json & json);
    Folder(string id, string accountId, int version);
    Folder(SQLite::Statement & query);

    json & localStatus();
    
    string path();
    void setPath(string path);
    
    string role() const;
    void setRole(string role);
  
    string tableName();
    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);

    void beforeSave(MailStore * store);
    void afterRemove(MailStore * store);
};

#endif /* Folder_hpp */
