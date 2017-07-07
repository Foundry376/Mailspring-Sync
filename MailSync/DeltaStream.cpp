//
//  DeltaStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "DeltaStream.hpp"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <functional>
#include <chrono>
#include <future>
#include <cstdio>

using json = nlohmann::json;


DeltaStream::DeltaStream() {
}


DeltaStream::~DeltaStream() {
}

json DeltaStream::waitForJSON() {
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

void DeltaStream::sendJSON(const json & msgJSON) {
    cout << msgJSON.dump() + "\n";
    cout << flush;
}

void DeltaStream::flushBuffer() {
    lock_guard<mutex> lock(bufferMtx);
    for (const auto & it : buffer) {
        for (const auto & msg : it.second) {
            sendJSON(msg);
        }
    }
    buffer = {};
    scheduled = false;
}

void DeltaStream::flushWithin(int ms) {
    std::chrono::system_clock::time_point desiredTime = std::chrono::system_clock::now();
    desiredTime += chrono::milliseconds(ms);

    if (!scheduled) {
        scheduledTime = desiredTime;
        scheduled = true;

        std::thread([this]() {
            std::unique_lock<std::mutex> lck(bufferFlushMtx);
            bufferFlushCv.wait_until(lck, this->scheduledTime);
            this->flushBuffer();
        }).detach();
    } else if (scheduled && (desiredTime < scheduledTime)) {
        std::unique_lock<std::mutex> lck(bufferFlushMtx);
        bufferFlushCv.notify_one();
    }
  
}

void DeltaStream::bufferMessage(string klass, string type, MailModel * model) {
    lock_guard<mutex> lock(bufferMtx);

    if (!buffer.count(klass)) {
        buffer[klass] = {};
    }
    if (buffer[klass].size() > 0 && buffer[klass].back()["type"].get<string>() == type) {
        buffer[klass].back()["objects"].push_back(model->toJSONDispatch());
    } else {
        json objs = json::array();
        objs.push_back(model->toJSONDispatch());
        buffer[klass].push_back({
            {"type", type},
            {"objectClass", klass},
            {"objects", objs},
        });
    }
}

void DeltaStream::didPersistModel(MailModel * model, int maxDeliveryDelay) {
    bufferMessage(model->tableName(), "persist", model);
    flushWithin(maxDeliveryDelay);
}

void DeltaStream::didUnpersistModel(MailModel * model, int maxDeliveryDelay) {
    bufferMessage(model->tableName(), "unpersist", model);
    flushWithin(maxDeliveryDelay);
}



