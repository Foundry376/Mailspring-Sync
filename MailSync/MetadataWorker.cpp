//
//  MetadataWorker.cpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MetadataWorker.hpp"
#include "MailStore.hpp"
#include "Message.hpp"
#include "Thread.hpp"
#include "Contact.hpp"
#include "SyncException.hpp"
#include "NetworkRequestUtils.hpp"

#include <string>
#include <curl/curl.h>

time_t backoffSeconds[] = {3, 3, 5, 10, 20, 30, 60, 120, 300, 300};
int backoffStep = 0;


static size_t _onDeltaData(void *contents, size_t length, size_t nmemb, void *userp) {
    MetadataWorker * worker = (MetadataWorker *)userp;
    size_t real_size = length * nmemb;
    worker->onDeltaData(contents, real_size);
    return real_size;
}


MetadataWorker::MetadataWorker(shared_ptr<Account> account) :
    store(new MailStore()),
    account(account),
    logger(spdlog::get("metadata"))
{
}

void MetadataWorker::run() {
    deltasCursor = store->getKeyValue("cursor-" + account->id());

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
                throw;
            }
            sleep(backoffSeconds[backoffStep]);
            if (backoffStep < 9)
                backoffStep += 1;
        }
    }
    
}

bool MetadataWorker::fetchMetadata(int page) {
    int pageSize = 500;
    const json & metadata = MakeAccountsRequest(account, "/metadata?limit=" + to_string(pageSize) + "&offset=" + to_string(pageSize * page));
    for (const auto & metadatum : metadata) {
        applyMetadataJSON(metadatum);
    }
    return metadata.size() == pageSize;
}

void MetadataWorker::fetchDeltaCursor() {
    const json & result = MakeAccountsRequest(account, "/delta/head");
    if (result == nullptr || !result.count("cursor")) {
        throw SyncException("no-cursor", "/delta/head API did not return JSON with a cursor", true);
    }
    if (result["cursor"].is_number()) {
        setDeltaCursor(to_string(result["cursor"].get<uint64_t>()));
    } else {
        setDeltaCursor(result["cursor"]);
    }
}

void MetadataWorker::fetchDeltasBlocking() {
    CURL * curl_handle = CreateAccountsRequest(account, "/delta/streaming?cursor=" + deltasCursor);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, _onDeltaData);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)this);

    logger->info("Delta stream starting...");
    CURLcode res = curl_easy_perform(curl_handle);
    logger->info("Delta stream closed.");
    
    if (res != CURLE_OK) {
        throw SyncException(res, "/delta/streaming");
    }

    curl_easy_cleanup(curl_handle);
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
            } catch (std::invalid_argument & ex) {
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
        logger->info("Received delta for unexpected object `{}`", klass);
    }

}

void MetadataWorker::applyMetadataJSON(const json & metadata) {
    // find the associated object
    string type = metadata["object_type"].get<string>();
    string id = metadata["object_id"].get<string>();
    string aid = metadata["account_id"].get<string>();
    string pluginId = metadata["plugin_id"].get<string>();
    const json & value = metadata["value"];
    uint32_t version = metadata["version"].get<uint32_t>();

    store->beginTransaction();

    unique_ptr<MailModel> model = store->findGeneric(type, Query().equal("id", id).equal("accountId", aid));

    if (model) {
        // attach the metadata to the object. Returns false if the model
        // already has a >= version of the metadata.
        if (model->upsertMetadata(pluginId, value, version) > 0) {
            store->save(model.get());
        }
    } else {
        // save to waiting table
        logger->info("Saving metadata for a model we haven't synced yet ({} ID {})", type, id);
        // TODO
    }
    store->commitTransaction();
}


