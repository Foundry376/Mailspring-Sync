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

#include "Identity.hpp"

#include <time.h>
#include <iomanip>
#include <sstream>

using namespace std;
using namespace mailcore;

// Singleton implementation

shared_ptr<Identity> Identity::_global = nullptr;

shared_ptr<Identity> Identity::GetGlobal() {
    return _global;
}

void Identity::SetGlobal(shared_ptr<Identity> i) {
    _global = i;
}


// Class implementation

Identity::Identity(json json) : MailModel(json) {
    
}

bool Identity::valid() {
    if (!_data.count("emailAddress") || !_data.count("token")) {
        return false;
    }
    return true;
}

time_t Identity::createdAt() {
    struct tm timeinfo {};
    memset(&timeinfo, 0, sizeof(struct tm));
    std::istringstream ss(_data["createdAt"].get<string>());
    ss >> std::get_time(&timeinfo, "%Y-%m-%dT%H:%M:%S.000Z");
    return mktime(&timeinfo);
}

string Identity::firstName() {
    return _data["firstName"].get<string>();
}

string Identity::lastName() {
    return _data["lastName"].get<string>();
}

string Identity::emailAddress() {
    return _data["emailAddress"].get<string>();
}

string Identity::token() {
    return _data["token"].get<string>();
}

/* Identity objects are not stored in the database. */

string Identity::tableName(){
    assert(false);
	return "";
}
string Identity::constructorName() {
    assert(false);
	return "";
}

vector<string> Identity::columnsForQuery() {
    assert(false);
	return {};
}

void Identity::bindToQuery(SQLite::Statement * query) {
    assert(false);
}
