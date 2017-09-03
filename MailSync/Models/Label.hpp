//
//  Folder.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef Label_hpp
#define Label_hpp

#include <stdio.h>
#include <string>
#include "json.hpp"

#include "Folder.hpp"

using namespace nlohmann;
using namespace std;

class Label : public Folder {
    
public:
    static string TABLE_NAME;

    Label(string id, string accountId, int version);
    Label(SQLite::Statement & query);

    string tableName();
};

#endif /* Folder_hpp */
