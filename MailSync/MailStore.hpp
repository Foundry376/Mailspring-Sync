//
//  MailStore.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef MailStore_hpp
#define MailStore_hpp

#include <stdio.h>
#include <vector>

#include <MailCore/MailCore.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include "json.hpp"

#include "Folder.hpp"
#include "Label.hpp"
#include "Message.hpp"
#include "Contact.hpp"
#include "Query.hpp"
#include "DeltaStream.hpp"
#include "MailUtils.hpp"

using namespace nlohmann;
using namespace std;

struct Metadata {
    int version;
    string objectId;
    string objectType;
    string accountId;
    string pluginId;
    json value;
};

Metadata MetadataFromJSON(const json & metadata);

struct MessageAttributes {
    uint32_t uid;
    bool unread;
    bool starred;
    bool draft;
    vector<string> labels;
};

MessageAttributes MessageAttributesForMessage(mailcore::IMAPMessage * msg);
bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b);


class MailStore {
    SQLite::Database _db;
    SQLite::Statement _stmtBeginTransaction;
    SQLite::Statement _stmtRollbackTransaction;
    SQLite::Statement _stmtCommitTransaction;
    
    bool _transactionOpen;
    vector<DeltaStreamItem> _transactionDeltas;

    map<string, shared_ptr<SQLite::Statement>> _saveUpdateQueries;
    map<string, shared_ptr<SQLite::Statement>> _saveInsertQueries;
    map<string, shared_ptr<SQLite::Statement>> _removeQueries;
    
    vector<shared_ptr<Label>> _labelCache;
    int _labelCacheVersion;
    int _streamMaxDelay;
    size_t _owningThread;
    
public:
    MailStore();

    void assertCorrectThread();

    void migrate();

    SQLite::Database & db();

    void resetForAccount(string accountId);
    
    string getKeyValue(string key);
    
    void saveKeyValue(string key, string value);
    
    void beginTransaction();
    
    void rollbackTransaction();

    void unsafeEraseTransactionDeltas();

    void commitTransaction();

    void save(MailModel * model);

    void saveFolderStatus(Folder * folder, json & initialLocalStatus);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, uint32_t depth, uint32_t before = UINT32_MAX);

    map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);

    vector<shared_ptr<Label>> allLabelsCache(string accountId);

    void setStreamDelay(int streamMaxDelay);
    
    // Detached plugin metadata storage

    vector<Metadata> findAndDeleteDetachedPluginMetadata(string accountId, string objectId);

    void saveDetachedPluginMetadata(Metadata & m);

    // Find - Not templated

    shared_ptr<MailModel> findGeneric(string type, Query query);
    
    vector<shared_ptr<MailModel>> findAllGeneric(string type, Query query);
    
    // Find - Template methods which must be defined in header file
    
    template<typename ModelClass>
    shared_ptr<ModelClass> find(Query & query) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.getSQL() + " LIMIT 1");
        query.bind(statement);
        if (statement.executeStep()) {
            return make_shared<ModelClass>(statement);
        }
        return nullptr;
    }
    
    template<typename ModelClass>
    vector<shared_ptr<ModelClass>> findAll(Query & query) {
        assertCorrectThread();
        string sql = "SELECT data FROM " + ModelClass::TABLE_NAME + query.getSQL();
        if (query.getLimit() != 0) {
            sql = sql + " LIMIT " + to_string(query.getLimit());
        }
        SQLite::Statement statement(this->_db, sql);
        query.bind(statement);
        
        vector<shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results.push_back(make_shared<ModelClass>(statement));
        }
        
        return results;
    }
    
    
    /**
     Handles dividing a large set into small chunks of <1000 and re-aggregating the results so SQLite can handle it.
     */
    template<typename ModelClass>
    vector<shared_ptr<ModelClass>> findLargeSet(std::string colname, vector<std::string> & set) {
        assertCorrectThread();
        
        vector<shared_ptr<ModelClass>> all;

        auto chunks = MailUtils::chunksOfVector(set, 900);
        for (auto chunk : chunks) {
            auto results = this->findAll<ModelClass>(Query().equal(colname, chunk));
            all.insert(all.end(), results.begin(), results.end());
        }
        
        return all;
    }
    
    
    
    template<typename ModelClass>
    map<string, shared_ptr<ModelClass>> findAllMap(Query & query, std::string keyField) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.getSQL());
        query.bind(statement);

        map<string, shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField.c_str()).getString()] = make_shared<ModelClass>(statement);
        }
        
        return results;
    }
    
    template<typename ModelClass>
    map<uint32_t, shared_ptr<ModelClass>> findAllUINTMap(Query & query, std::string keyField) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.getSQL());
        query.bind(statement);

        map<uint32_t, shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField.c_str()).getUInt()] = make_shared<ModelClass>(statement);
        }
        
        return results;
    }
    
    void remove(MailModel * model);
    
    template<typename ModelClass>
    void remove(Query & query) {
        assertCorrectThread();
        auto models = findAll<ModelClass>(query);

        SQLite::Statement statement(this->_db, "DELETE FROM " + ModelClass::TABLE_NAME + query.getSQL());
        query.bind(statement);
        statement.exec();
        
        DeltaStreamItem delta {DELTA_TYPE_UNPERSIST, models};
        _emit(delta);
    }
    

private:

    void _emit(DeltaStreamItem & delta);
};


#endif /* MailStore_hpp */
