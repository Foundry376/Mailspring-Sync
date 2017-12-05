//
//  DeltaStream.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
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

// DeltaStreamItem

DeltaStreamItem::DeltaStreamItem(string type, string modelClass, vector<json> inJSONs) :
    type(type), modelClass(modelClass)
{
    for (const auto & itemJSON : inJSONs) {
        upsertModelJSON(itemJSON);
    }
}

DeltaStreamItem::DeltaStreamItem(string type, MailModel * model) :
    type(type), modelClass(model->tableName())
{
    upsertModelJSON(model->toJSONDispatch());
}

DeltaStreamItem::DeltaStreamItem(string type, vector<shared_ptr<MailModel>> & models) :
    type(type), modelClass("none"), modelJSONs({})
{
    if (models.size() > 0) {
        modelClass = models[0]->tableName();
        for (const auto & m : models) {
            upsertModelJSON(m->toJSONDispatch());
        }
    }
}

bool DeltaStreamItem::concatenate(const DeltaStreamItem & other) {
    if (other.type != type || other.modelClass != modelClass) {
        return false;
    }

    // If we already have v1 of a model (specifically v1), we can't accept v2.
    // It would mean the client couldn't detect a "create" reliably.
    for (const auto & modelJSON : other.modelJSONs) {
        string id = modelJSON["id"].get<string>();
        // if we have this model and it's version is 1, return false
        if (idIndexes.count(id) && modelJSONs[idIndexes[id]]["v"].get<int>() == 1) {
            return false;
        }
    }

    for (const auto & modelJSON : other.modelJSONs) {
        upsertModelJSON(modelJSON);
    }
    return true;
}

void DeltaStreamItem::upsertModelJSON(const json & item) {
    // scan and replace any instance of the object already available, or append.
    // It's important two back-to-back saves of the same object don't create two entries,
    // only the last one.
    string id = item["id"].get<string>();

    if (idIndexes.count(id)) {
        modelJSONs[idIndexes[id]] = item;
    } else {
        idIndexes[id] = modelJSONs.size();
        modelJSONs.push_back(item);
    }
}

string DeltaStreamItem::dump() const {
    json j = {
        {"type", type},
        {"modelJSONs", modelJSONs},
        {"modelClass", modelClass}
    };
    return j.dump();
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

void DeltaStream::flushBuffer() {
    lock_guard<mutex> lock(bufferMtx);
    for (const auto & it : buffer) {
        for (const auto & item : it.second) {
            cout << item.dump() + "\n";
            cout << flush;
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

void DeltaStream::queueDeltaForDelivery(DeltaStreamItem item) {
    lock_guard<mutex> lock(bufferMtx);

    if (!buffer.count(item.modelClass)) {
        buffer[item.modelClass] = {};
    }
    if (buffer[item.modelClass].size() == 0 || !buffer[item.modelClass].back().concatenate(item)) {
        buffer[item.modelClass].push_back(item);
    }
}

void DeltaStream::emit(DeltaStreamItem item, int maxDeliveryDelay) {
    queueDeltaForDelivery(item);
    flushWithin(maxDeliveryDelay);
}

void DeltaStream::emit(vector<DeltaStreamItem> items, int maxDeliveryDelay) {
    for (const auto item : items) {
        queueDeltaForDelivery(item);
    }
    flushWithin(maxDeliveryDelay);
}



