//
//  MailModel.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailModel.hpp"
#include "MailUtils.hpp"

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
            _initialMetadataPluginIds[m["pluginId"].get<string>()] = true;
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

void MailModel::writeAssociations(SQLite::Database & db) {
    
    map<string, bool> metadataPluginIds{};
    if (_data.count("metadata")) {
        for (const auto & m : _data["metadata"]) {
            metadataPluginIds[m["pluginId"].get<string>()] = true;
        }
    }
    
    // update the ThreadCategory join table to include our folder and labels
    // note this is pretty expensive, so we avoid it if relevant attributes
    // have not changed since the model was loaded.
    if (_initialMetadataPluginIds != metadataPluginIds) {
        string _id = id();
        SQLite::Statement removePluginIds(db, "DELETE FROM " + this->tableName() + "PluginMetadata WHERE id = ?");
        removePluginIds.bind(1, _id);
        removePluginIds.exec();
        
        string qmarks = MailUtils::qmarkSets(metadataPluginIds.size(), 2);
        
        SQLite::Statement insertPluginIds(db, "INSERT INTO " + this->tableName() + "PluginMetadata (id, value) VALUES " + qmarks);
        
        int i = 1;
        for (auto& it : metadataPluginIds) {
            insertPluginIds.bind(i++, _id);
            insertPluginIds.bind(i++, it.first);
        }
        insertPluginIds.exec();
    }

}


