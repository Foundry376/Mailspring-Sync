//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Label.hpp"
#include "MailUtils.hpp"

using namespace std;

Label::Label(string id, string accountId, int version) :
    Folder(id, accountId, version)
{
}

Label::Label(SQLite::Statement & query) :
    Folder(query)
{
}

string Label::TABLE_NAME = "Label";

string Label::tableName() {
    return Label::TABLE_NAME;
}
