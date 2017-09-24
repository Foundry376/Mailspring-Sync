//
//  MetadataExpirationManager.cpp
//  MailSync
//
//  Created by Ben Gotow on 9/11/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MetadataExpirationWorker.hpp"
#include "DeltaStream.hpp"

// Singleton Implementation

map<string, MetadataExpirationWorker*> _workerMap;

MetadataExpirationWorker * MetadataExpirationWorkerForAccountId(string aid) {
    return _workerMap[aid];
}

MetadataExpirationWorker::MetadataExpirationWorker(string accountId) :
    _accountId(accountId)
{
    _workerMap[accountId] = this;
}

// Called from all threads

void MetadataExpirationWorker::didSaveMetadataWithExpiration(long e) {
    std::chrono::system_clock::time_point eTime = std::chrono::system_clock::from_time_t(e);

    if (eTime < _wakeTime) {
        // wake the expiration watcher - it will process expired metadata, find this new
        // expiration value, and sleep until that time instead of the old wakeTime.
        std::unique_lock<std::mutex> lck(_wakeMtx);
        _wakeCv.notify_one();
    }
}

// Called on dedicated thread

void MetadataExpirationWorker::run() {
    auto logger(spdlog::get("logger"));
    
    // wait at least 15 seconds before sending the first metadata expiration event,
    // because plugins make take longer than the main application to load!
    sleep(15);
    
    while (true) {
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
                auto models = store.findAllGeneric(pair.first, Query().equal("id", pair.second));
                for (auto & model : models) {
                    logger->info("-- Sending expiration event for {} {}", pair.first, model->id());
                    SharedDeltaStream()->emit(DeltaStreamItem(DELTA_TYPE_METADATA_EXPIRATION, model.get()), 500);
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
            
            // always wait one extra second - don't wake /exactly/ when the metadata expires
            // in case we're somehow scheduled and wake early.
            next += 1;
            
            // always wait at least fifteen seconds - this ensures we don't create a performance
            // disaster if for some reason metadata gets "stuck" on items.
            if (next < now + 15) {
                next = now + 15;
            }
            
            // sleep until then, or we're woken to lower the delay
            logger->info("-- Will wake for next expiration in {}sec", next - now);
            _wakeTime = std::chrono::system_clock::from_time_t((time_t)next);
        }

        std::unique_lock<std::mutex> lck(_wakeMtx);
        _wakeCv.wait_until(lck, _wakeTime);
    }
}
