//
//  DeltaStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "DeltaStream.hpp"
#include "ThreadUtils.h"

#include <stdio.h>
#include <string.h>

#ifndef _MSC_VER
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <stdlib.h>
#include <functional>
#include <chrono>
#include <future>
#include <cstdio>

using namespace nlohmann;

// Singleton Implementation

shared_ptr<DeltaStream> _globalStream = make_shared<DeltaStream>();

shared_ptr<DeltaStream> SharedDeltaStream() {
    return _globalStream;
}

// Class

DeltaStream::DeltaStream() : scheduled(false) {
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
        //
    } catch (...) {
        return {}; // ok
    }
    return {};
}

void DeltaStream::sendJSON(const json & msgJSON) {
//    spdlog::get("logger")->info("{}", msgJSON.dump());
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
    lock_guard<mutex> lock(bufferMtx);

    if (!scheduled) {
        scheduledTime = desiredTime;
        scheduled = true;

        std::thread([this]() {
            SetThreadName("DeltaStreamFlush");
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
        // scan and replace any instance of the object already available, or append.
        // It's important two back-to-back saves of the same object don't create two entries,
        // only the last one.
        auto & delta = buffer[klass].back();
        bool found = false;
        for (int ii = 0; ii < delta["objects"].size(); ii ++) {
            if (delta["objects"][ii]["id"].get<string>() == model->id()) {
                delta["objects"][ii] = model->toJSONDispatch();
                found = true;
                break;
            }
        }
        if (!found) {
            delta["objects"].push_back(model->toJSONDispatch());
        }
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

void DeltaStream::emitPersistModel(MailModel * model, int maxDeliveryDelay) {
    bufferMessage(model->tableName(), "persist", model);
    flushWithin(maxDeliveryDelay);
}

void DeltaStream::emitUnpersistModel(MailModel * model, int maxDeliveryDelay) {
    bufferMessage(model->tableName(), "unpersist", model);
    flushWithin(maxDeliveryDelay);
}

void DeltaStream::emitMetadataExpiration(MailModel * model, int maxDeliveryDelay) {
    bufferMessage(model->tableName(), "metadata-expiration", model);
    flushWithin(maxDeliveryDelay);
}



