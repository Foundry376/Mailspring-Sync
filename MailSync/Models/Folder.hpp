//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Folder_hpp
#define Folder_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"

#include "MailModel.hpp"

using json = nlohmann::json;

class Folder : public MailModel {
protected:
    std::string _path;
    std::string _role;
    json _localStatus;
    
public:
    Folder(std::string id, std::string accountId, int version);
    Folder(SQLite::Statement & query);

    json & localStatus();
    
    std::string path();
    void setPath(std::string path);
    
    std::string role();
    void setRole(std::string role);
  
    std::string tableName();
    std::vector<std::string> columnsForQuery();
    void bindToQuery(SQLite::Statement & query);

    json toJSON();
};

#endif /* Folder_hpp */
