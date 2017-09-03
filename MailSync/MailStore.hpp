//
//  MailStore.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
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

using namespace nlohmann;
using namespace std;

struct MessageAttributes {
    uint32_t uid;
    bool unread;
    bool starred;
    bool draft;
    vector<string> labels;
};

MessageAttributes MessageAttributesForMessage(mailcore::IMAPMessage * msg);
bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b);


// Base class
class MailStore;

class MailStoreObserver {
public:
    virtual void didPersistModel(MailStore * store, MailModel * model) = 0;
    virtual void didUnpersistModel(MailStore * store, MailModel * model) = 0;
};


class MailStore {
    SQLite::Database _db;
    SQLite::Statement _stmtBeginTransaction;
    SQLite::Statement _stmtRollbackTransaction;
    SQLite::Statement _stmtCommitTransaction;
    map<string, shared_ptr<SQLite::Statement>> _saveUpdateQueries;
    map<string, shared_ptr<SQLite::Statement>> _saveInsertQueries;
    map<string, shared_ptr<SQLite::Statement>> _removeQueries;
    
    vector<shared_ptr<Label>> _labelCache;
    bool _labelCacheInvalid;
    int _streamMaxDelay;
    
public:
    MailStore();

    void migrate();

    SQLite::Database & db();

    string getKeyValue(string key);
    
    void saveKeyValue(string key, string value);
    
    void beginTransaction();
    
    void rollbackTransaction();

    void commitTransaction();

    void save(MailModel * model, bool emit = true);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, uint32_t depth, uint32_t before = UINT32_MAX);

    map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);
    
    vector<shared_ptr<Label>> allLabelsCache(string accountId);

    void setStreamDelay(int streamMaxDelay);
    
    shared_ptr<MailModel> findGeneric(string type, Query query);
    
    // Template methods which must be defined in header file
    
    template<typename ModelClass>
    shared_ptr<ModelClass> find(Query & query) {
        SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.sql() + " LIMIT 1");
        query.bind(statement);
        if (statement.executeStep()) {
            return make_shared<ModelClass>(statement);
        }
        return nullptr;
    }
    
    template<typename ModelClass>
    vector<shared_ptr<ModelClass>> findAll(Query & query) {
        SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.sql());
        query.bind(statement);
        
        vector<shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results.push_back(make_shared<ModelClass>(statement));
        }
        
        return results;
    }
    
    template<typename ModelClass>
    map<string, shared_ptr<ModelClass>> findAllMap(Query & query, std::string keyField) {
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.sql());
        query.bind(statement);

        map<string, shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField.c_str()).getString()] = make_shared<ModelClass>(statement);
        }
        
        return results;
    }
    
    template<typename ModelClass>
    map<uint32_t, shared_ptr<ModelClass>> findAllUINTMap(Query & query, std::string keyField) {
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.sql());
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
        auto results = findAll<ModelClass>(query);

        SQLite::Statement statement(this->_db, "DELETE FROM " + ModelClass::TABLE_NAME + query.sql());
        query.bind(statement);
        statement.exec();
        
        for (auto & result : results) {
            SharedDeltaStream()->didUnpersistModel(result.get(), _streamMaxDelay);
        }
    }
    

private:
    void notify(string type);
};


#endif /* MailStore_hpp */
