//
//  MailStore.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailStore.hpp"
#include "MailUtils.hpp"
#include "constants.h"

#import "Folder.hpp"
#import "Message.hpp"
#import "Thread.hpp"

using namespace mailcore;
using namespace std;

#pragma mark MessageAttributes

MessageAttributes MessageAttributesForMessage(IMAPMessage * msg) {
    auto m = MessageAttributes{};
    m.uid = msg->uid();
    m.unread = bool(!(msg->flags() & MessageFlagSeen));
    m.starred = bool(msg->flags() & MessageFlagFlagged);
    m.labels = std::vector<std::string>{};

    Array * labels = msg->gmailLabels();
    if (labels != nullptr) {
        for (int ii = 0; ii < labels->count(); ii ++) {
            string str = ((String *)labels->objectAtIndex(ii))->UTF8Characters();
            // Gmail exposes Trash and Spam as folders and labels. We want them
            // to be folders so we ignore their presence as labels.
            if ((str == "\\Trash") || (str == "\\Spam")) {
                continue;
            }
            m.labels.push_back(str);
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
    _stmtBeginTransaction(_db, "BEGIN IMMEDIATE TRANSACTION"),
    _stmtRollbackTransaction(_db, "ROLLBACK"),
    _stmtCommitTransaction(_db, "COMMIT"),
    _labelCacheInvalid(true),
    _labelCache()
{
    _db.setBusyTimeout(10 * 1000);
    
    for (string sql : SETUP_QUERIES) {
        SQLite::Statement(_db, sql).exec();
    }
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

void MailStore::beginTransaction() {
    _stmtBeginTransaction.exec();
    _stmtBeginTransaction.reset();
}


void MailStore::rollbackTransaction() {
    // Note: when a transaction is interrupted and we roll it back,
    // running the statement again produces the error again? Unclear...
    _saveUpdateQueries = {};
    _saveInsertQueries = {};
    _removeQueries = {};
    _stmtRollbackTransaction.exec();
    _stmtRollbackTransaction.reset();
}

void MailStore::commitTransaction() {
    _stmtCommitTransaction.exec();
    _stmtCommitTransaction.reset();
}

void MailStore::save(MailModel * model) {
    model->incrementVersion();
    
    auto tableName = model->tableName();
    
    if (model->version() > 1) {
        if (!_saveUpdateQueries.count(tableName)) {
            string pairs{""};
            for (const auto col : model->columnsForQuery()) {
                if (col == "id") {
                    continue;
                }
                pairs += (col + " = :" + col + ",");
            }
            pairs.pop_back();
            
            auto stmt = make_shared<SQLite::Statement>(this->_db, "UPDATE " + tableName + " SET " + pairs + " WHERE id = :id");
            _saveUpdateQueries[tableName] = stmt;
        }
        auto query = _saveUpdateQueries[tableName];
        query->reset();
        model->bindToQuery(query.get());
        query->exec();
        
    } else {
        if (!_saveInsertQueries.count(tableName)) {
            string cols{""};
            string values{""};
            for (const auto col : model->columnsForQuery()) {
                cols += col + ",";
                values += ":" + col + ",";
            }
            cols.pop_back();
            values.pop_back();
            
            auto stmt = make_shared<SQLite::Statement>(this->_db, "INSERT INTO " + tableName + " (" + cols + ") VALUES (" + values + ")");
            _saveInsertQueries[tableName] = stmt;
        }
        auto query = _saveInsertQueries[tableName];
        query->reset();
        model->bindToQuery(query.get());
        query->exec();
    }

    model->writeAssociations(this->_db);

    if (tableName == "Label") {
        _labelCacheInvalid = true;
    }
    for (auto & observer : this->_observers) {
        observer->didPersistModel(model);
    }
}

void MailStore::remove(MailModel * model) {
    auto tableName = model->tableName();
    if (!_removeQueries.count(tableName)) {
        _removeQueries[tableName] = make_shared<SQLite::Statement>(this->_db, "DELETE FROM " + tableName + " WHERE id = ?");
    }
    auto query = _removeQueries[tableName];
    query->reset();
    query->bind(1, model->id());
    query->exec();

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
