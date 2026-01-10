//
//  MetadataWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MetadataWorker.hpp"
#include "MailStore.hpp"
#include "MailStoreTransaction.hpp"
#include "MailUtils.hpp"
#include "Message.hpp"
#include "Thread.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"
#include "exceptions.h"

#include <string>
#include <curl/curl.h>

time_t backoffSeconds[] = {3, 3, 5, 10, 20, 30, 60, 120, 300, 300};


static size_t _onDeltaData(void *contents, size_t length, size_t nmemb, void *userp) {
    MetadataWorker * worker = (MetadataWorker *)userp;
    size_t real_size = length * nmemb;
    worker->onDeltaData(contents, real_size);
    return real_size;
}


MetadataWorker::MetadataWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("logger"))
{
}

void MetadataWorker::run() {
    deltasCursor = store->getKeyValue("cursor-" + account->id());
    backoffStep = 0;

    if (Identity::GetGlobal() == nullptr) {
        logger->info("Metadata sync disabled, not logged in.");
        return;
    }

    while(true) {
        try {
            // If we don't have a cursor (we're just starting for the first time),
            // obtain a cursor for "now" and paginate through all existing metadata.
            // By the time we finish, now will be out of date but inclusive of all
            // changes since we started.
            if (deltasCursor == "") {
                fetchDeltaCursor();
                
                int page = 0;
                bool more = true;
                while (more == true) {
                    more = fetchMetadata(page++);
                }
            }

            // Open the streaming connection and block until the connection is broken
            fetchDeltasBlocking();
            backoffStep = 0;

        } catch (SyncException & ex) {
            if (!ex.isRetryable()) {
                exceptions::logCurrentExceptionWithStackTrace();
                abort();
            }
            logger->info("Will retry in {} sec.", backoffSeconds[backoffStep]);
            MailUtils::sleepWorkerUntilWakeOrSec(backoffSeconds[backoffStep]);
            if (backoffStep < 9)
                backoffStep += 1;
        } catch (...) {
            exceptions::logCurrentExceptionWithStackTrace();
            abort();
        }
    }
    
}

bool MetadataWorker::fetchMetadata(int page) {
    int pageSize = 500;
    const json & metadata = PerformIdentityRequest("/metadata/" + account->id() + "?limit=" + to_string(pageSize) + "&offset=" + to_string(pageSize * page));
    for (const auto & metadatum : metadata) {
        applyMetadataJSON(metadatum);
    }
    return metadata.size() == pageSize;
}

void MetadataWorker::fetchDeltaCursor() {
    const json & result = PerformIdentityRequest("/deltas/" + account->id() + "/head");
    if (result == nullptr || !result.count("cursor")) {
        logger->info("Unexpected response from /delta/head: {}", result ? result.dump() : "nullptr");
        throw SyncException("no-cursor", "/delta/head API did not return JSON with a cursor", true);
    }
    if (result["cursor"].is_number()) {
        setDeltaCursor(to_string(result["cursor"].get<uint64_t>()));
    } else {
        setDeltaCursor(result["cursor"]);
    }
}

void MetadataWorker::fetchDeltasBlocking() {
    string platform = "linux";
#if defined(_MSC_VER)
    platform = "win32";
#endif
#if defined(__APPLE__)
    platform = "darwin";
#endif
    const char * a = account->IMAPHost().c_str();
    char * aEscapedPtr = curl_escape(a, (int)strlen(a));
    string aEscaped = aEscapedPtr;
    curl_free(aEscapedPtr);

    CURL * curl_handle = CreateIdentityRequest("/deltas/" + account->id() + "/streaming?p=" + platform + "&ih=" + aEscaped + "&cursor=" + deltasCursor);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onDeltaData);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)this);

    // close the connection if we receive <1 byte/sec for 30 seconds.
    // the backend sends 16 bytes (16 x "\n") every 10 sec, giving
    // 1.06 - 1.6 bytes every 30 sec depending on whether 2 or 3 packets
    // arrive.
    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);

    // todo - potentially switch to a mode like this example that
    // would allow us to control how often the thread wakes:
    // https://curl.haxx.se/libcurl/c/multi-single.html
    logger->info("Metadata delta stream starting...");
    CURLcode res = curl_easy_perform(curl_handle);
    if (res == CURLE_OPERATION_TIMEDOUT) {
        logger->info("Metadata delta stream timed out.");
    } else {
        logger->info("Metadata delta stream closed.");
    }

    ValidateRequestResp(res, curl_handle, "");
    CleanupCurlRequest(curl_handle);
}

void MetadataWorker::setDeltaCursor(string cursor) {
    deltasCursor = cursor;
    store->saveKeyValue("cursor-" + account->id(), deltasCursor);
}

void MetadataWorker::onDeltaData(void * contents, size_t bytes) {
    _onAppendToString(contents, bytes, 1, &deltasBuffer);
    
    bool matched = true;
    while (matched) {
        size_t pos = deltasBuffer.find("\n");
        if (pos == string::npos) {
            matched = false;
            break;
        }

        if (pos > 1) { // ignore heartbeat newlines
            string delta { deltasBuffer.substr(0, pos) };
            
            try {
                json deltaJSON = json::parse(delta);
                onDelta(deltaJSON);
            } catch (json::exception & ex) {
                json resp = {{"error", ex.what()}};
                this->logger->error("Received invalid JSON in server delta stream: {}", delta);
            }
        }

        deltasBuffer = deltasBuffer.substr(pos + 1, deltasBuffer.length() - pos);
    }
}

void MetadataWorker::onDelta(const json & delta) {
    string klass = delta["object"].get<string>() ;
    if (klass == "metadata") {
        applyMetadataJSON(delta["attributes"]);
        setDeltaCursor(delta["cursor"]);
    } else {
        logger->info("Received delta of unexpected type `{}`", klass);
    }
}

void MetadataWorker::applyMetadataJSON(const json & metadataJSON) {
    // find the associated object
    auto m = MetadataFromJSON(metadataJSON);
    {
        MailStoreTransaction transaction{store, "applyMetadataJSON"};

        auto model = store->findGeneric(m.objectType, Query().equal("id", m.objectId).equal("accountId", m.accountId));

        logger->info("Received metadata V{} for ({} - {})", m.version, m.objectType, m.objectId);

        if (model) {
            // attach the metadata to the object. Returns false if the model
            // already has a >= version of the metadata.
            if (model->upsertMetadata(m.pluginId, m.value, m.version) > 0) {
                logger->info(" -- Saved on to local model.");
                store->save(model.get());
            } else {
                logger->info(" -- Ignored. Local model has >= version.");
            }
        } else {
            // save to waiting table - when mailsync saves this model, it will attach
            // and remove the metadata if it's available
            logger->info(" -- Local model is not present. Saving to waiting table.");
            store->saveDetachedPluginMetadata(m);
        }
        
        transaction.commit();
    }
}


