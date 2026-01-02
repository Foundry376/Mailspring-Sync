//
//  MailModel.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailModel.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"
#include "SyncException.hpp"
#include "MetadataExpirationWorker.hpp"

using namespace std;

string MailModel::TABLE_NAME = "MailModel";

/* Note: If creating a brand new object, pass version = 0. */
MailModel::MailModel(string id, string accountId, int version) :
    _data({{"id", id}, {"aid", accountId}, {"v", version}})
{
    captureInitialMetadataState();
}

MailModel::MailModel(SQLite::Statement & query) :
    _data(json::parse(query.getColumn("data").getString()))
{
    captureInitialMetadataState();
}


MailModel::MailModel(json json) :
    _data(json)
{
    assert(_data.is_object());
    captureInitialMetadataState();
}

void MailModel::captureInitialMetadataState() {
    _initialMetadataPluginIds = {};
    if (_data.count("metadata")) {
        for (const auto & m : _data["metadata"]) {
            _initialMetadataPluginIds[m["pluginId"].get<string>()] = m["v"].get<int>();
        }
    }
}

string MailModel::id()
{
    return _data["id"].get<std::string>();
}

string MailModel::accountId()
{
    return _data["aid"].get<std::string>();
}

int MailModel::version()
{
    return _data["v"].get<int>();
}

void MailModel::incrementVersion()
{
    _data["v"] = _data["v"].get<int>() + 1;
}

bool MailModel::supportsMetadata() {
    return false;
}

int MailModel::upsertMetadata(string pluginId, const json & value, int version)
{
    assert(supportsMetadata());
    
    if (!_data.count("metadata")) {
        _data["metadata"] = json::array();
    }
    for (auto & m : _data["metadata"]) {
        if (m["pluginId"].get<string>() == pluginId) {
            if (version != -1 && m["v"].get<int>() >= version) {
                return -1;
            }
            int nextVersion = version == -1 ? (m["v"].get<uint32_t>() + 1) : version;
            m["value"] = value;
            m["v"] = nextVersion;
            return nextVersion;
        }
    }
    
    int nextVersion = version == -1 ? 1 : version;
    _data["metadata"].push_back({
        {"pluginId", pluginId},
        {"value", value},
        {"v", nextVersion}
    });
    return nextVersion;
}

json & MailModel::metadata() {
    return _data["metadata"];
}

string MailModel::tableName()
{
    return TABLE_NAME;
}

json MailModel::toJSON()
{
    // note: do not override for Task!
    if (!_data.count("__cls")) {
        _data["__cls"] = this->tableName();
    }
    return _data;
}

json MailModel::toJSONDispatch()
{
    return this->toJSON();
}

void MailModel::bindToQuery(SQLite::Statement * query) {
    auto _id = id();
    query->bind(":id", _id);
    query->bind(":data", this->toJSON().dump());
    query->bind(":accountId", accountId());
    query->bind(":version", version());

    if (id() != _id) {
        throw SyncException("assertion-failure", "The ID of a model changed while it was being serialized. How can this happen?", false);
    }
}

void MailModel::beforeSave(MailStore * store) {
    if (version() == 1 && supportsMetadata()) {
        // look for any pending metadata we need to attach to ourselves
        vector<Metadata> metadatas = store->findAndDeleteDetachedPluginMetadata(accountId(), id());

        for (auto & m : metadatas) {
            spdlog::get("logger")->info("-- Attaching waiting metadata for {}", m.pluginId);
            upsertMetadata(m.pluginId, m.value, m.version);
        }
    }
}

void MailModel::afterSave(MailStore * store) {
    if (!supportsMetadata()) {
        return;
    }

    map<string, int> metadataPluginIds{};
    if (_data.count("metadata")) {
        for (const auto & m : _data["metadata"]) {
            metadataPluginIds[m["pluginId"].get<string>()] = m["v"].get<int>();
        }
    }
    
    // update the ThreadCategory join table to include our folder and labels
    // note this is pretty expensive, so we avoid it if relevant attributes
    // have not changed since the model was loaded.
    if (_initialMetadataPluginIds != metadataPluginIds) {
        string _id = id();
        
        SQLite::Statement removePluginIds(store->db(), "DELETE FROM ModelPluginMetadata WHERE id = ?");
        removePluginIds.bind(1, _id);
        removePluginIds.exec();
        
        SQLite::Statement insertPluginIds(store->db(), "INSERT INTO ModelPluginMetadata (id, accountId, objectType, value, expiration) VALUES (?,?,?,?, ?)");
        insertPluginIds.bind(1, _id);
        insertPluginIds.bind(2, accountId());
        insertPluginIds.bind(3, this->tableName());
        
        long lowestExpiration = LONG_MAX;

        for (const auto & m : _data["metadata"]) {
            // metadata without any contents is omitted from the join table
            // so it's possible to "remove" metadata while keeping the versions
            // incrementing forever.
            if (m["value"].size() == 0) {
                continue;
            }

            bool hasExpiration = m["value"].count("expiration") && m["value"]["expiration"].is_number();

            insertPluginIds.bind(4, m["pluginId"].get<string>());
            if (hasExpiration) {
                long e = m["value"]["expiration"].get<long>();
                if (e < lowestExpiration) { lowestExpiration = e; }
                insertPluginIds.bind(5, (long long)e);
            } else {
                insertPluginIds.bind(5); // binds null
            }
            insertPluginIds.exec();
            insertPluginIds.reset();
        }

        if (lowestExpiration != LONG_MAX) {
            // tell the delta stream that we've just changed metadata
            // so it can invalidate / recalculate expiration timers
            MetadataExpirationWorker * worker = MetadataExpirationWorkerForAccountId(accountId());
            if (worker != nullptr) {
                worker->isSavingMetadataWithExpiration(lowestExpiration);
            }
        }
    }
}

void MailModel::afterRemove(MailStore * store) {
    // delete metadata entries
    if (!supportsMetadata()) {
        return;
    }
    string _id = id();
    SQLite::Statement removePluginIds(store->db(), "DELETE FROM ModelPluginMetadata WHERE id = ?");
    removePluginIds.bind(1, _id);
    removePluginIds.exec();
}

