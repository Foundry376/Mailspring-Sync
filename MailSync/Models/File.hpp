//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef File_hpp
#define File_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"
#include "spdlog/spdlog.h"

#include "MailModel.hpp"
#include "MailStore.hpp"
#include <MailCore/MailCore.h>

using json = nlohmann::json;
using namespace std;
using namespace mailcore;

class Message;

class File : public MailModel {
    
public:
    static string TABLE_NAME;

    File(Message * msg, Attachment * a);
    File(json json);
    File(SQLite::Statement & query);
  
    string filename();
    string safeFilename();
    string partId();
        
    string tableName();
    string constructorName();

    vector<string> columnsForQuery();
    void bindToQuery(SQLite::Statement * query);
};

#endif /* File_hpp */
