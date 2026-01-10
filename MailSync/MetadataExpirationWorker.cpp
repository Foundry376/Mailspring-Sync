//
//  MetadataExpirationManager.cpp
//  MailSync
//
//  Created by Ben Gotow on 9/11/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MetadataExpirationWorker.hpp"
#include "DeltaStream.hpp"
#include "MailUtils.hpp"

// Singleton Implementation

map<string, MetadataExpirationWorker*> _workerMap;

MetadataExpirationWorker * MetadataExpirationWorkerForAccountId(string aid) {
    return _workerMap[aid];
}

void WakeAllMetadataExpirationWorkers() {
    for (auto pair : _workerMap) {
        pair.second->isSavingMetadataWithExpiration(0);
    }
}

MetadataExpirationWorker::MetadataExpirationWorker(string accountId) :
    _accountId(accountId)
{
    _workerMap[accountId] = this;
}

// Called from all threads

void MetadataExpirationWorker::isSavingMetadataWithExpiration(long e) {
    std::chrono::system_clock::time_point eTime = std::chrono::system_clock::from_time_t(e);

    // Lock before checking _wakeTime to avoid TOCTOU race condition.
    // The run() thread also holds this lock when modifying _wakeTime.
    std::unique_lock<std::mutex> lck(_wakeMtx);
    if (eTime < _wakeTime) {
        // wake the expiration watcher - it will process expired metadata, find this new
        // expiration value, and sleep until that time instead of the old wakeTime.
        _wakeCv.notify_all();
    } else {
        spdlog::get("logger")->info("Metadata expiration {} is further in future than wake time {}", e, std::chrono::system_clock::to_time_t(_wakeTime));
    }
}

// Called on dedicated thread

void MetadataExpirationWorker::run() {
    auto logger(spdlog::get("logger"));
    
    // Until we run once, no need to bother locking the mutex and waking us
    _wakeTime = std::chrono::system_clock::from_time_t(0);

    // wait at least 15 seconds before sending the first metadata expiration event,
    // because plugins make take longer than the main application to load!
    std::this_thread::sleep_for(std::chrono::seconds(15));
    
    while (true) {
        std::chrono::system_clock::time_point nextWakeTime;
        {
            MailStore store;
            
            logger->info("Scanning for expired metadata");

            long long now = time(0);
            SQLite::Statement find(store.db(), "SELECT objectType, id FROM ModelPluginMetadata WHERE accountId = ? AND expiration <= ?");
            find.bind(1, _accountId);
            find.bind(2, now);
            map<string, vector<string>> results;
            while (find.executeStep()) {
                string objectType = find.getColumn("objectType").getString();
                string objectId = find.getColumn("id").getString();
                if (!results.count(objectType)) {
                    results[objectType] = {};
                }
                results[objectType].push_back(objectId);
            }
            
            // iterate through the types, load the models and dispatch them
            for (auto & pair : results) {
                auto chunks = MailUtils::chunksOfVector(pair.second, 100);
                size_t remaining = chunks.size();
                
                for (auto chunk : chunks) {
                    auto models = store.findAllGeneric(pair.first, Query().equal("id", chunk));
                    for (auto & model : models) {
                        logger->info("-- Sending expiration event for {} {}", pair.first, model->id());
                        SharedDeltaStream()->emit(DeltaStreamItem(DELTA_TYPE_METADATA_EXPIRATION, model.get()), 500);
                    }
                    remaining -= 1;
                    if (remaining > 0) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }

            // find the next metadata expiration
            SQLite::Statement findNext(store.db(), "SELECT expiration FROM ModelPluginMetadata WHERE accountId = ? AND expiration > ?");
            findNext.bind(1, _accountId);
            findNext.bind(2, now);
            
            long long next = now + 2 * 60 * 60; // 2 hours
            if (findNext.executeStep()) {
                next = findNext.getColumn("expiration").getInt64();
            }
            
            // If we just sent expiration events, wait at least 15 seconds before doing another pass,
            // to avoid nasty issues like repeat sending, etc. if the client takes a few seconds to
            // remove the metadata. If we didn't find anything (eg we were awoken by new metadata),
            // wait 5 seconds (min interval for undo send) just to be safe.
            int minTimeDelay = results.size() > 0 ? 15 : 5;
            if (next < now + minTimeDelay) {
                next = now + minTimeDelay;
            }
            
            // sleep until then, or we're woken to lower the delay
            logger->info("-- Will wake for next expiration in {}sec", next - now);
            nextWakeTime = std::chrono::system_clock::from_time_t((time_t)next);
        }

        std::unique_lock<std::mutex> lck(_wakeMtx);
        _wakeTime = nextWakeTime;
        _wakeCv.wait_until(lck, _wakeTime);
        
        // we've been woken up! Wait one second before processing.
        // We don't want to trigger at /exactly/ the time we're asked in case the client's
        // time is slightly different. Also, because we can be woken by a model's metadata
        // changing (INSIDE a transaction block) we need to wait to let the transaction
        // commit before querying metadata expiration timestamps (Yes this is sort of a hack.)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
