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
#include "Query.hpp"

using json = nlohmann::json;
using namespace std;

struct MessageAttributes {
    uint32_t uid;
    bool unread;
    bool starred;
    vector<string> labels;
};

MessageAttributes MessageAttributesForMessage(mailcore::IMAPMessage * msg);
bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b);


// Base class
class MailStoreObserver {
public:
    virtual void didPersistModel(MailModel * model) = 0;
    virtual void didUnpersistModel(MailModel * model) = 0;
};


class MailStore {
    SQLite::Database _db;
    SQLite::Statement _stmtBeginTransaction;
    SQLite::Statement _stmtCommitTransaction;
    vector<MailStoreObserver*> _observers;
    vector<shared_ptr<Label>> _labelCache;
    bool _labelCacheInvalid;

public:
    MailStore();
    
    SQLite::Database & db();

    void beginTransaction();

    void commitTransaction();

    void save(MailModel * model);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, int depth, int before = UINT32_MAX);

    map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);
    
    vector<shared_ptr<Label>> allLabelsCache();

    void addObserver(MailStoreObserver * observer);
    
    // Template methods which must be defined in header file
    
    template<typename ModelClass>
    unique_ptr<ModelClass> find(Query & query) {
        SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.sql() + " LIMIT 1");
        query.bind(statement);
        if (statement.executeStep()) {
            return unique_ptr<ModelClass>(new ModelClass(statement));
        }
        return unique_ptr<ModelClass>(nullptr);
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
            for (auto & observer : this->_observers) {
                observer->didUnpersistModel(result.get());
            }
        }
    }
    

private:
    void notify(string type);
};


#endif /* MailStore_hpp */
