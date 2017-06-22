//
//  MailStore.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailStore.hpp"
#include "MailUtils.hpp"

#import "Folder.hpp"
#import "Message.hpp"
#import "Thread.hpp"

using namespace mailcore;
using namespace std;

#pragma mark MessageAttributes

MessageAttributes MessageAttributesForMessage(mailcore::IMAPMessage * msg) {
    auto m = MessageAttributes{};
    m.uid = msg->uid();
    m.unread = bool(!(msg->flags() & mailcore::MessageFlagSeen));
    m.starred = bool(msg->flags() & mailcore::MessageFlagFlagged);
    m.labels = std::vector<std::string>{};

    Array * labels = msg->gmailLabels();
    if (labels != nullptr) {
        for (int ii = 0; ii < labels->count(); ii ++) {
            m.labels.push_back(((String *)labels->objectAtIndex(ii))->UTF8Characters());
        }
        sort(m.labels.begin(), m.labels.end());
    }
    
    return m;
}

bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b) {
    return a.unread == b.unread && a.starred == b.starred && a.uid == b.uid && a.labels == b.labels;
}


#pragma mark MailStore

MailStore::MailStore() :
    _db("/Users/bengotow/.nylas-dev/edgehill.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
    _labelCacheInvalid(true),
    _labelCache()
{
    SQLite::Statement mQuery(this->_db, "CREATE TABLE IF NOT EXISTS Message ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "headerMessageId VARCHAR(255),"
                             "gMsgId VARCHAR(255),"
                             "gThrId VARCHAR(255),"
                             "subject VARCHAR(500),"
                             "date DATETIME,"
                             "draft TINYINT(1),"
                             "isSent TINYINT(1),"
                             "unread TINYINT(1),"
                             "starred TINYINT(1),"
                             "replyToMessageId VARCHAR(255),"
                             "folderImapUID INTEGER,"
                             "folderImapXGMLabels TEXT,"
                             "folderId VARCHAR(65),"
                             "threadId VARCHAR(65))");
    mQuery.exec();
    
    
    SQLite::Statement fQuery(this->_db, "CREATE TABLE IF NOT EXISTS Folder ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "path VARCHAR(255),"
                             "role VARCHAR(255),"
                             "createdAt DATETIME,"
                             "updatedAt DATETIME)");
    fQuery.exec();
    
    SQLite::Statement lQuery(this->_db, "CREATE TABLE IF NOT EXISTS Label ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "path VARCHAR(255),"
                             "role VARCHAR(255),"
                             "createdAt DATETIME,"
                             "updatedAt DATETIME)");
    lQuery.exec();
    
    SQLite::Statement tQuery(this->_db, "CREATE TABLE IF NOT EXISTS Thread ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "gThrId VARCHAR(20),"
                             "subject VARCHAR(500),"
                             "snippet VARCHAR(255),"
                             "unread INTEGER,"
                             "starred INTEGER,"
                             "firstMessageTimestamp DATETIME,"
                             "lastMessageTimestamp DATETIME,"
                             "lastMessageReceivedTimestamp DATETIME,"
                             "lastMessageSentTimestamp DATETIME,"
                             "inAllMail TINYINT(1),"
                             "isSearchIndexed TINYINT(1),"
                             "participants TEXT,"
                             "hasAttachments INTEGER)");
    tQuery.exec();
    
    // TODO add unique keys

    SQLite::Statement tfQuery(this->_db, "CREATE TABLE IF NOT EXISTS ThreadCategory ("
                              "id VARCHAR(65),"
                              "value VARCHAR(65),"
                              "inAllMail TINYINT(1),"
                              "unread TINYINT(1),"
                              "lastMessageReceivedTimestamp DATETIME,"
                              "lastMessageSentTimestamp DATETIME,"
                              "categoryId VARCHAR(65),"
                              "PRIMARY KEY (id, value))");
    tfQuery.exec();
    
    SQLite::Statement trQuery(this->_db, "CREATE TABLE IF NOT EXISTS ThreadReference ("
                             "threadId VARCHAR(65),"
                             "headerMessageId VARCHAR(255),"
                             "PRIMARY KEY (threadId, headerMessageId))");
    trQuery.exec();

    Query q;
}


SQLite::Database & MailStore::db()
{
    return this->_db;
}

map<uint32_t, MessageAttributes> MailStore::fetchMessagesAttributesInRange(Range range, Folder & folder) {
    SQLite::Statement query(this->_db, "SELECT id, unread, starred, folderImapUID, folderImapXGMLabels FROM Message WHERE folderId = ? AND folderImapUID >= ? AND folderImapUID <= ?");
    query.bind(1, folder.id());
    query.bind(2, (long long)(range.location));
    query.bind(3, (long long)(range.location + range.length));
    
    map<uint32_t, MessageAttributes> results {};

    while (query.executeStep()) {
        MessageAttributes attrs{};
        uint32_t uid = (uint32_t)query.getColumn("folderImapUID").getInt64();
        attrs.uid = uid;
        attrs.starred = query.getColumn("starred").getInt() != 0;
        attrs.unread = query.getColumn("unread").getInt() != 0;
        
        vector<string> labels{};
        for (const auto i : json::parse(query.getColumn("folderImapXGMLabels").getString())) {
            labels.push_back(i.get<string>());
        }
        attrs.labels = labels;

        results[uid] = attrs;
    }
    
    return results;
}

uint32_t MailStore::fetchMessageUIDAtDepth(Folder & folder, int depth, int before) {
    SQLite::Statement query(this->_db, "SELECT folderImapUID FROM Message WHERE folderId = ? AND folderImapUID < ? ORDER BY folderImapUID DESC LIMIT 1 OFFSET ?");
    query.bind(1, folder.id());
    query.bind(2, before);
    query.bind(3, depth);
    if (query.executeStep()) {
        return query.getColumn("folderImapUID").getUInt();
    }
    return 1;
}

vector<shared_ptr<Label>> MailStore::allLabelsCache() {
    if (_labelCacheInvalid) {
        Query q{};
        _labelCache = findAll<Label>(q);
        _labelCacheInvalid = false;
    }
    return _labelCache;
}

void MailStore::save(MailModel * model) {
    model->incrementVersion();
    
    if (model->version() > 1) {
        string pairs{""};
        for (const auto col : model->columnsForQuery()) {
            if (col == "id") {
                continue;
            }
            pairs += (col + " = :" + col + ",");
        }
        pairs.pop_back();
        
        SQLite::Statement query(this->_db, "UPDATE " + model->tableName() + " SET " + pairs + " WHERE id = :id");
        model->bindToQuery(query);
        query.exec();
        
    } else {
        string cols{""};
        string values{""};
        for (const auto col : model->columnsForQuery()) {
            cols += col + ",";
            values += ":" + col + ",";
        }
        cols.pop_back();
        values.pop_back();

        SQLite::Statement query(this->_db, "INSERT INTO " + model->tableName() + " (" + cols + ") VALUES (" + values + ")");
        model->bindToQuery(query);
        query.exec();
    }

    model->writeAssociations(this->_db);

    if (model->tableName() == "Label") {
        _labelCacheInvalid = true;
    }
    for (auto & observer : this->_observers) {
        observer->didPersistModel(model);
    }
}

void MailStore::remove(MailModel * model) {
    SQLite::Statement query(this->_db, "DELETE FROM " + model->tableName() + " WHERE id = :id LIMIT 1");
    query.bind(":id", model->id());
    query.exec();

    if (model->tableName() == "Label") {
        _labelCacheInvalid = true;
    }
    for (auto & observer : this->_observers) {
        observer->didUnpersistModel(model);
    }
}

#pragma mark Events

void MailStore::addObserver(MailStoreObserver * observer) {
    this->_observers.push_back(observer);
}
