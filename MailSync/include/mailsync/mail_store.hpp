/** MailStore [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MailStore_hpp
#define MailStore_hpp

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include <memory>
#include <vector>

#include "MailCore/MailCore.h"
#include "SQLiteCpp/SQLiteCpp.h"
#include "nlohmann/json.hpp"

#include "mailsync/models/folder.hpp"
#include "mailsync/models/label.hpp"
#include "mailsync/models/message.hpp"
#include "mailsync/models/contact.hpp"
#include "mailsync/query.hpp"
#include "mailsync/delta_stream.hpp"
#include "mailsync/mail_utils.hpp"

struct Metadata {
    int version;
    std::string objectId;
    std::string objectType;
    std::string accountId;
    std::string pluginId;
    nlohmann::json value;
};

Metadata MetadataFromJSON(const nlohmann::json & metadata);

struct MessageAttributes {
    uint32_t uid;
    bool unread;
    bool starred;
    bool draft;
    std::vector<std::string> labels;
};

MessageAttributes MessageAttributesForMessage(mailcore::IMAPMessage * msg);
bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b);


class MailStore {
    SQLite::Database _db;
    SQLite::Statement _stmtBeginTransaction;
    SQLite::Statement _stmtRollbackTransaction;
    SQLite::Statement _stmtCommitTransaction;

    bool _transactionOpen;
    std::vector<DeltaStreamItem> _transactionDeltas;

    std::map<std::string, std::shared_ptr<SQLite::Statement>> _saveUpdateQueries;
    std::map<std::string, std::shared_ptr<SQLite::Statement>> _saveInsertQueries;
    std::map<std::string, std::shared_ptr<SQLite::Statement>> _removeQueries;

    std::vector<std::shared_ptr<Label>> _labelCache;
    int _labelCacheVersion;
    int _streamMaxDelay;
    size_t _owningThread;

public:
    MailStore();

    void assertCorrectThread();

    void migrate();

    SQLite::Database & db();

    void resetForAccount(std::string accountId);

    std::string getKeyValue(std::string key);

    void saveKeyValue(std::string key, std::string value);

    void beginTransaction();

    void rollbackTransaction();

    void unsafeEraseTransactionDeltas();

    void commitTransaction();

    void save(MailModel * model);

    void saveFolderStatus(Folder * folder, nlohmann::json & initialLocalStatus);

    uint32_t fetchMessageUIDAtDepth(Folder & folder, uint32_t depth, uint32_t before = UINT32_MAX);

    std::map<uint32_t, MessageAttributes> fetchMessagesAttributesInRange(mailcore::Range range, Folder & folder);

    std::vector<std::shared_ptr<Label>> allLabelsCache(std::string accountId);

    void setStreamDelay(int streamMaxDelay);

    // Detatched plugin metadata storage

    std::vector<Metadata> findAndDeleteDetatchedPluginMetadata(std::string accountId, std::string objectId);

    void saveDetatchedPluginMetadata(Metadata & m);

    // Find - Not templated

    std::shared_ptr<MailModel> findGeneric(std::string type, Query query);

    std::vector<std::shared_ptr<MailModel>> findAllGeneric(std::string type, Query query);

    // Find - Template methods which must be defined in header file

    template<typename ModelClass>
    std::shared_ptr<ModelClass> find(Query & query) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.getSQL() + " LIMIT 1");
        query.bind(statement);
        if (statement.executeStep()) {
            return std::make_shared<ModelClass>(statement);
        }
        return nullptr;
    }

    template<typename ModelClass>
    std::vector<std::shared_ptr<ModelClass>> findAll(Query & query) {
        assertCorrectThread();
        std::string sql = "SELECT data FROM " + ModelClass::TABLE_NAME + query.getSQL();
        if (query.getLimit() != 0) {
            sql = sql + " LIMIT " + std::to_string(query.getLimit());
        }
        SQLite::Statement statement(this->_db, sql);
        query.bind(statement);

        std::vector<std::shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results.push_back(std::make_shared<ModelClass>(statement));
        }

        return results;
    }


    /**
     Handles dividing a large set into small chunks of <1000 and re-aggregating the results so SQLite can handle it.
     */
    template<typename ModelClass>
    std::vector<std::shared_ptr<ModelClass>> findLargeSet(std::string colname, std::vector<std::string> & set) {
        assertCorrectThread();

        std::vector<std::shared_ptr<ModelClass>> all;

        auto chunks = MailUtils::chunksOfVector(set, 900);
        for (auto chunk : chunks) {
            auto results = this->findAll<ModelClass>(Query().equal(colname, chunk));
            all.insert(all.end(), results.begin(), results.end());
        }

        return all;
    }



    template<typename ModelClass>
    std::map<std::string, std::shared_ptr<ModelClass>> findAllMap(Query & query, std::string keyField) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.getSQL());
        query.bind(statement);

        std::map<std::string, std::shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField.c_str()).getString()] = std::make_shared<ModelClass>(statement);
        }

        return results;
    }

    template<typename ModelClass>
    std::map<uint32_t, std::shared_ptr<ModelClass>> findAllUINTMap(Query & query, std::string keyField) {
        assertCorrectThread();
        SQLite::Statement statement(this->_db, "SELECT " + keyField + ", data FROM " + ModelClass::TABLE_NAME + query.getSQL());
        query.bind(statement);

        std::map<uint32_t, std::shared_ptr<ModelClass>> results;
        while (statement.executeStep()) {
            results[statement.getColumn(keyField.c_str()).getUInt()] = std::make_shared<ModelClass>(statement);
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
