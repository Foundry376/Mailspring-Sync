//
//  CommStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "CommStream.hpp"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>

using json = nlohmann::json;

CommStream::CommStream() {
}


CommStream::~CommStream() {
}

json CommStream::waitForJSON() {
    try {
        string buffer;
        cin.clear();
        cin.sync();
        getline(cin, buffer);
        if (buffer.size() > 0) {
            json j = json::parse(buffer);
            return j;
        }
    } catch (char const * e) {
    }
    return {};
}

void CommStream::sendJSON(json & msgJSON) {
    lock_guard<mutex> lock(mtx_);
    cout << msgJSON.dump() + "\n";
    cout << flush;
}

void CommStream::didPersistModel(MailModel * model) {
    json msg = {
        {"type", "persist"},
        {"objectClass", model->tableName()},
        {"object", model->toJSONDispatch()},
    };
    sendJSON(msg);
}

void CommStream::didUnpersistModel(MailModel * model) {
    json msg = {
        {"type", "unpersist"},
        {"objectClass", model->tableName()},
        {"object", model->toJSONDispatch()},
    };
    sendJSON(msg);
}

