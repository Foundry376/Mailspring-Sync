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
#include "Message.hpp"
#include "Query.hpp"

using json = nlohmann::json;

struct MessageAttributes {
    uint32_t uid;
    int version;
    bool seen;
    bool flagged;
};

// Base class
class MailStoreObserver {
public:
    virtual void didPersistModel(MailModel * model) = 0;
    virtual void didUnpersistModel(MailModel * model) = 0;
};


class MailStore {
    SQLite::Database _db;
    std::vector<MailStoreObserver*> _observers;
    
public:
    MailStore();
    
    void insertMessage(mailcore::IMAPMessage * mMsg, Folder & folder);
        
    void save(MailModel * model);

    void remove(MailModel * model);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, int depth);

    std::map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);
        
    void updateMessageAttributes(MessageAttributes local, mailcore::IMAPMessage * remoteMsg, Folder & folder);
    
    void deleteMessagesWithUIDs(std::vector<uint32_t> & uids, Folder & folder);

    void addObserver(MailStoreObserver * observer);
    
    // Template methods which must be defined in header file
    
    template<typename ModelClass> std::unique_ptr<ModelClass> find(Query & query) {
        SQLite::Statement statement(this->_db, "SELECT * FROM " + ModelClass::TABLE_NAME + query.sql() + " LIMIT 1");
        query.bind(statement);
        if (statement.executeStep()) {
            return std::unique_ptr<ModelClass>(new ModelClass(statement));
        }
        return std::unique_ptr<ModelClass>(nullptr);
    }
    
    template<typename ModelClass> std::map<std::string, std::shared_ptr<ModelClass>> findAllMap(Query & query, const char* keyField) {
        SQLite::Statement statement(this->_db, "SELECT * FROM " + ModelClass::TABLE_NAME + query.sql());
        query.bind(statement);

        std::map<std::string, std::shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField).getString()] = std::make_shared<ModelClass>(statement);
        }
        
        return results;
    }
    
    template<typename ModelClass> std::map<uint32_t, std::shared_ptr<ModelClass>> findAllUINTMap(Query & query, const char* keyField) {
        SQLite::Statement statement(this->_db, "SELECT * FROM " + ModelClass::TABLE_NAME + query.sql());
        query.bind(statement);

        std::map<uint32_t, std::shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField).getUInt()] = std::make_shared<ModelClass>(statement);
        }
        
        return results;
    }
    

private:
    void notify(std::string type);
};


#endif /* MailStore_hpp */
