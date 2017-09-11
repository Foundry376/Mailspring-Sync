//
//  MailModel.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailModel.hpp"
#include "MailUtils.hpp"
#include "MailStore.hpp"

using namespace std;

string MailModel::TABLE_NAME = "MailModel";

/* Note: If creating a brand new object, pass version = 0. */
MailModel::MailModel(string id, string accountId, int version) :
    _data({{"id", id},{"aid", accountId}, {"v", version}})
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

int MailModel::upsertMetadata(string pluginId, const json & value, int version)
{
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
    query->bind(":id", id());
    query->bind(":data", this->toJSON().dump());
    query->bind(":accountId", accountId());
    query->bind(":version", version());
}

void MailModel::beforeSave(MailStore * store) {
    if (version() == 1) {
        // look for any pending metadata we need to attach to ourselves
        vector<Metadata> metadatas = store->findAndDeleteDetatchedPluginMetadata(accountId(), id());
        spdlog::get("logger")->info("-- Looking for metadata for {}", id());

        for (auto & m : metadatas) {
            spdlog::get("logger")->info("-- Attaching waiting metadata for {}", m.pluginId);
            upsertMetadata(m.pluginId, m.value, m.version);
        }
    }
}

void MailModel::afterSave(MailStore * store) {
    
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
        
        SQLite::Statement insertPluginIds(store->db(), "INSERT INTO ModelPluginMetadata (id, objectType, value, expiration) VALUES (?,?,?,?)");
        insertPluginIds.bind(1, _id);
        insertPluginIds.bind(2, this->tableName());
        for (const auto & m : _data["metadata"]) {
            insertPluginIds.bind(3, m["pluginId"].get<string>());
            if (m["value"].count("expiration") && m["value"]["expiration"].is_number()) {
                insertPluginIds.bind(4, m["value"]["expiration"].get<int32_t>());
            } else {
                insertPluginIds.bind(4); // binds null
            }
            insertPluginIds.exec();
            insertPluginIds.reset();
        }
    }
}

void MailModel::afterRemove(MailStore * store) {
    // delete metadata entries
    string _id = id();
    SQLite::Statement removePluginIds(store->db(), "DELETE FROM ModelPluginMetadata WHERE id = ?");
    removePluginIds.bind(1, _id);
    removePluginIds.exec();
}

