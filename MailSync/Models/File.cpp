//
//  Folder.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
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
    MailModel(MailUtils::idForFile(msg, a), msg->accountId(), 0)
{
    _data["messageId"] = msg->id();
    _data["partId"] = a->partID()->UTF8Characters();
    
    if (a->isInlineAttachment() && a->contentID()) {
        _data["contentId"] = a->contentID()->UTF8Characters();
    }
    if (a->mimeType()) {
        _data["contentType"] = a->mimeType()->UTF8Characters();
    }
    
    string name = "";
    if (a->filename()) {
        name = a->filename()->UTF8Characters();
    }
    if (name == "") {
        name = "Unnamed Attachment";

        string type = _data["contentType"];
        if (type == "text/calendar") {
            name = "Event.ics";
        }
        if (type == "image/png" || type == "image/x-png") {
            name = "Unnamed Image.png";
        }
        if (type == "image/jpg") {
            name = "Unnamed Image.jpg";
        }
        if (type == "image/jpeg") {
            name = "Unnamed Image.jpg";
        }
        if (type == "image/gif") {
            name = "Unnamed Image.gif";
        }
        if (type == "message/delivery-status") {
            name = "Delivery Status.txt";
        }
        if (type == "message/feedback-report") {
            name = "Feedback Report.txt";
        }
    }
    
    _data["filename"] = name;
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

/*
 The name of the file inside its files/<id>/ directory. The client records the name it wrote a
 draft's attachment under (`localFilename`), so the engine never re-derives the client's
 sanitized name; files without one were written by the engine under safeFilename().
*/
string File::filenameOnDisk() {
    if (_data.count("localFilename") && _data["localFilename"].is_string()) {
        string name = _data["localFilename"].get<string>();
        // It is joined onto the directory's path, so it must name an entry inside it.
        if (name != "" && name != "." && name != ".." && name.find_first_of(string("/\\\0", 3)) == string::npos) {
            return name;
        }
    }
    return safeFilename();
}

string File::partId() {
    return _data["partId"].get<string>();
}

json & File::contentId() {
    return _data["contentId"];
}

void File::setContentId(string s) {
    _data["contentId"] = s;
}

string File::contentType() {
    return _data["contentType"].get<string>();
}

vector<string> File::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "filename"};
}

void File::bindToQuery(SQLite::Statement * query) {
    MailModel::bindToQuery(query);
    query->bind(":filename", filename());
}
