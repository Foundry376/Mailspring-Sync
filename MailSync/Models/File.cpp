//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
#include <regex>

#include "File.hpp"
#include "MailUtils.hpp"
#include "Thread.hpp"
#include "Message.hpp"

using namespace std;
using namespace mailcore;

string File::TABLE_NAME = "File";

File::File(Message * msg, Attachment * a) :
    MailModel(MailUtils::idRandomlyGenerated(), msg->accountId(), 0)
{
    _data["messageId"] = msg->id();
    _data["partId"] = a->partID()->UTF8Characters();
    
    if (a->isInlineAttachment() && a->contentID()) {
        _data["contentId"] = a->contentID()->UTF8Characters();
    }
    if (a->mimeType()) {
        _data["contentType"] = a->mimeType()->UTF8Characters();
    }
    if (a->filename()) {
        _data["filename"] = a->filename()->UTF8Characters();
    } else {
        _data["filename"] = "";
    }
    _data["size"] = a->data()->length();
}

File::File(json json) : MailModel(json) {
    
}

File::File(SQLite::Statement & query) :
    MailModel(query)
{
}

string File::constructorName() {
    return _data["__cls"].get<string>();
}

string File::tableName() {
    return File::TABLE_NAME;
}

string File::filename() {
    return _data["filename"].get<string>();
}

string File::safeFilename() {
    regex e ("[\\/:|?*><\"#]");
    return regex_replace (filename(), e, "-");
}

string File::partId() {
    return _data["partId"].get<string>();
}

vector<string> File::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "filename"};
}

void File::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":filename", filename());
}
