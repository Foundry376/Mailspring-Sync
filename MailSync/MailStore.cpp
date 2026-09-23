//
//  MailStore.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#include "MailStore.hpp"
#include "MailUtils.hpp"
#include "MailStoreTransaction.hpp"
#include "SyncException.hpp"
#include "constants.h"

#include "Folder.hpp"
#include "Message.hpp"
#include "Thread.hpp"

#include <filesystem>
#include <algorithm>
#include <set>
#include <stdexcept>

using namespace mailcore;
using namespace std;

std::atomic<int> globalLabelsVersion {1};
std::atomic<int> globalFoldersVersion {1};

#pragma mark Metadata

Metadata MetadataFromJSON(const json & metadata) {
    Metadata m;
    m.objectType = metadata["object_type"].get<string>();
    m.objectId = metadata["object_id"].get<string>();
    m.accountId = metadata["aid"].get<string>();
    m.pluginId = metadata["plugin_id"].get<string>();
    m.version = metadata["v"].get<uint32_t>();
    m.value = metadata["value"];
    return m;
}

#pragma mark MessageAttributes

MessageAttributes MessageAttributesForMessage(IMAPMessage * msg) {
    auto m = MessageAttributes{};
    m.uid = msg->uid();
    m.unread = bool(!(msg->flags() & MessageFlagSeen));
    m.starred = bool(msg->flags() & MessageFlagFlagged);
    m.labels = std::vector<std::string>{};
    
    Array * labels = msg->gmailLabels();
    bool draftLabelPresent = false;
    bool trashSpamLabelPresent = false;
    if (labels != nullptr) {
        for (unsigned int ii = 0; ii < labels->count(); ii ++) {
            string str = ((String *)labels->objectAtIndex(ii))->UTF8Characters();
            // Gmail exposes Trash and Spam as folders and labels. We want them
            // to be folders so we ignore their presence as labels.
            if ((str == "\\Trash") || (str == "\\Spam")) {
                trashSpamLabelPresent = true;
                continue;
            }
            if ((str == "\\Draft")) {
                draftLabelPresent = true;
            }
            m.labels.push_back(str);
        }
        sort(m.labels.begin(), m.labels.end());
    }
    
    m.draft = (bool(msg->flags() & MessageFlagDraft) || draftLabelPresent) && !trashSpamLabelPresent;
    
    return m;
}

bool MessageAttributesMatch(MessageAttributes a, MessageAttributes b) {
    return a.unread == b.unread && a.starred == b.starred && a.draft == b.draft && a.uid == b.uid && a.labels == b.labels;
}


#pragma mark MailStore

MailStore::MailStore() :
    _db(MailUtils::getEnvUTF8("CONFIG_DIR_PATH") + FS_PATH_SEP + "edgehill.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
    _stmtBeginTransaction(_db, "BEGIN IMMEDIATE TRANSACTION"),
    _stmtRollbackTransaction(_db, "ROLLBACK"),
    _stmtCommitTransaction(_db, "COMMIT"),
    _owningThread(spdlog::details::os::thread_id()),
    _labelCacheVersion(0),
    _labelCache(),
    _folderCacheVersion(0),
    _folderCache()
{
    _db.setBusyTimeout(60 * 1000);
    
    // Note: These are properties of the connection, so they must be set regardless
    // of whether the database setup queries are run.
    
    // https://www.sqlite.org/intern-v-extern-blob.html
    // A database page size of 8192 or 16384 gives the best performance for large BLOB I/O.
    SQLite::Statement(_db, "PRAGMA journal_mode = WAL").executeStep();
    SQLite::Statement(_db, "PRAGMA main.page_size = 4096").exec();
    SQLite::Statement(_db, "PRAGMA main.cache_size = 10000").exec();
    SQLite::Statement(_db, "PRAGMA main.synchronous = NORMAL").exec();
}

static int CURRENT_VERSION = 10;
static string VACUUM_TIME_KEY = "VACUUM_TIME";
static time_t VACUUM_INTERVAL = 30 * 24 * 60 * 60; // 30 days

void MailStore::migrate() {
    SQLite::Statement uv(_db, "PRAGMA user_version");
    uv.executeStep();
    int version = uv.getColumn(0).getInt();
    uv.reset();
    
    string verb = version == 0 ? "Setup" : "Migration";
    
    if (version < 1) {
        for (string sql : V1_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 3) {
        // This one will be time consuming - display window
        cout << "\nRunning " << verb;
        cout.flush();
        for (string sql : V3_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 4) {
        for (string sql : V4_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 6) {
        for (string sql : V6_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 7) {
        for (string sql : V7_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 8) {
        for (string sql : V8_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 9) {
        for (string sql : V9_SETUP_QUERIES) {
            SQLite::Statement(_db, sql).exec();
        }
    }
    if (version < 10) {
        _migrateToV10(version == 0, verb);
    }

    // Update the version flag. Note that we don't want to go from v3 back to v2
    // if the user re-opens an older version of the app.
    if (version < CURRENT_VERSION) {
        SQLite::Statement(_db, "PRAGMA user_version = " + to_string(CURRENT_VERSION)).exec();
    }

    // Initialize VACUUM timer if we're on version 0, but make everyone coming
    // from old versions VACUUM for the first time.
    if (version == 0) saveKeyValue(VACUUM_TIME_KEY, to_string(time(0)));
    string vacuumTimeS = getKeyValue(VACUUM_TIME_KEY);
    time_t vacuumTime = vacuumTimeS != "" ? stol(vacuumTimeS) : 0;

    // VACUUM if it's been a while
    if (time(0) - vacuumTime > VACUUM_INTERVAL) {
        cout << "\nRunning Vacuum\n";
        cout.flush();
        
        // Update vacuum timer first so we don't re-attempt vacuuming if it fails
        saveKeyValue(VACUUM_TIME_KEY, to_string(time(0)));
        
        try {
            SQLite::Statement(_db, "VACUUM").exec();
        } catch (std::exception & ex) {
            // Vacuuming can fail if we run out of disk space and isn't mandatory,
            // so we fail silently and still return 0 to allow the app to launch.
            cout << "\n" << "Vacuuming failed with SQLite error:";
            cout << "\n" << ex.what();
        }
    }
}

/*
 V10 creates MessageFolder, backfills one placement per message and rebuilds the Message
 table without its location columns (see constants.h). It runs in one explicit transaction
 so a crash mid-way rolls back the DDL, the backfill and the rebuild together and leaves
 user_version at the old value for the next launch to retry. A fresh database already has
 the final Message shape from V1 and only needs the new table and its indexes.

 Disk check: the rebuilt table and the placements are written to the WAL and checkpointed
 into the main file, whose pages from the dropped table are only reclaimed by the 30-day
 VACUUM. On a 1.15 GB / 258k-message database the file grows by 385 MB and the WAL peaks at
 about the same, so 1.5x the database size is a safe ceiling. The failure is a plain
 runtime_error naming the disk so the client's "problem with your local email database"
 dialog does not read like corruption.
 */
void MailStore::_migrateToV10(bool freshDatabase, const string & verb) {
    if (!freshDatabase) {
        SQLite::Statement pageCount(_db, "PRAGMA page_count");
        pageCount.executeStep();
        SQLite::Statement pageSize(_db, "PRAGMA page_size");
        pageSize.executeStep();
        unsigned long long databaseBytes = (unsigned long long)pageCount.getColumn(0).getInt64() * (unsigned long long)pageSize.getColumn(0).getInt64();
        unsigned long long neededBytes = databaseBytes + databaseBytes / 2;

        std::error_code ec;
        auto space = std::filesystem::space(MailUtils::getEnvUTF8("CONFIG_DIR_PATH"), ec);
        if (!ec && space.available < neededBytes) {
            throw std::runtime_error(
                "Mailspring needs about " + to_string(neededBytes / (1024 * 1024)) +
                " MB of free disk space to upgrade its mail database, but only " +
                to_string(space.available / (1024 * 1024)) +
                " MB is available on the volume holding your Mailspring data. " +
                "Your mail database is intact - free up space and relaunch Mailspring.");
        }

        // The client watches for this line and shows its progress window.
        cout << "\nRunning " << verb;
        cout.flush();
    }

    SQLite::Statement(_db, "BEGIN IMMEDIATE TRANSACTION").exec();
    try {
        vector<vector<string> *> steps = {&V10_SETUP_QUERIES, &V10_INDEX_QUERIES};
        if (!freshDatabase) {
            steps.insert(steps.begin() + 1, &V10_UPGRADE_QUERIES);
        }
        for (auto queries : steps) {
            for (string sql : *queries) {
                SQLite::Statement(_db, sql).exec();
            }
        }
        SQLite::Statement(_db, "PRAGMA user_version = 10").exec();
        SQLite::Statement(_db, "COMMIT").exec();
    } catch (...) {
        try {
            SQLite::Statement(_db, "ROLLBACK").exec();
        } catch (...) {
            // The failure that got us here is the one worth reporting.
        }
        throw;
    }

    if (!freshDatabase) {
        SQLite::Statement placements(_db, "SELECT COUNT(*) FROM MessageFolder");
        placements.executeStep();
        SQLite::Statement orphans(_db, "SELECT COUNT(*) FROM MessageOrphan");
        orphans.executeStep();
        cout << "\nMigration V10: " << placements.getColumn(0).getInt64() << " placements created, "
             << orphans.getColumn(0).getInt64() << " messages without a copy";
        cout.flush();
    }
}

void MailStore::assertCorrectThread() {
    /* Because we re-use SQLite prepared statements and a single SQLite connection
     per worker, it's extremely important that all calls to each MailStore are made
     from a single thread. We capture a threadId when you open the MailStore and
     require that all subseuqent calls are from that thread.
     
     Otherwise, it's possible for two threads to bind to the same prepared query,
     prepare half the values, and execute it, creating a rediculous data inconsistency.
     */
    if (spdlog::details::os::thread_id() != _owningThread) {
        // Null before main() registers the logger, which the --mode migrate path never
        // reaches. The throw below is what actually reports this; losing the log line is
        // better than turning an assertion into a segfault. See MailStoreTransaction::commit.
        if (auto logger = spdlog::get("logger")) {
            logger->error("MailStore thread assertion failure: function called on {} instead of {}", spdlog::details::os::thread_id(), _owningThread);
        }
        throw SyncException("assertion-failure", "MailStore thread assertion failure", false);
    }
}

void MailStore::resetForAccount(string accountId) {
    assertCorrectThread();
    for (string sql : ACCOUNT_RESET_QUERIES) {
        SQLite::Statement statement {_db, sql };
        statement.bind(1, accountId);
        statement.exec();
    }
    
    // reset the metadata stream cursor so we re-fetch metadata on resync
    saveKeyValue("cursor-" + accountId, "0");

    SQLite::Statement(_db, "VACUUM").exec();
}

SQLite::Database & MailStore::db()
{
    return this->_db;
}

// Both range reads are served by MessageFolder rather than Message so the fat JSON row
// is never visited. The explicit `remoteUID > 0` term is what lets the planner use the
// partial MessageFolderUIDIndex.
map<uint32_t, MessageAttributes> MailStore::fetchMessagesAttributesInRange(Range range, Folder & folder) {
    assertCorrectThread();
    auto & query = _placementStatement("attrsInRange",
        "SELECT remoteUID, unread, starred, draft, remoteXGMLabels FROM MessageFolder "
        "WHERE accountId = ? AND folderId = ? AND remoteUID >= ? AND remoteUID <= ? AND remoteUID > 0");
    query.bind(1, folder.accountId());
    query.bind(2, folder.id());
    query.bind(3, (long long)(range.location));
    
    // Range is uint64_t, and "*" is represented by UINT64_MAX.
    // SQLite doesn't support UINT64 and the conversion /can/ fail.
    // Additionally, clamp to LLONG_MAX if the sum would overflow.
    if (range.length == UINT64_MAX) {
        query.bind(4, LLONG_MAX);
    } else {
        uint64_t rangeEnd = range.location + range.length;
        if (rangeEnd > static_cast<uint64_t>(LLONG_MAX)) {
            query.bind(4, LLONG_MAX);
        } else {
            query.bind(4, static_cast<long long>(rangeEnd));
        }
    }

    map<uint32_t, MessageAttributes> results {};

    while (query.executeStep()) {
        MessageAttributes attrs{};
        uint32_t uid = (uint32_t)query.getColumn("remoteUID").getInt64();
        attrs.uid = uid;
        attrs.starred = query.getColumn("starred").getInt() != 0;
        attrs.unread = query.getColumn("unread").getInt() != 0;
        attrs.draft = query.getColumn("draft").getInt() != 0;
        
        // Note: parse must not throw here - the statement is cached and an unwind would
        // leave its cursor open, pinning the WAL read snapshot until the next pass.
        vector<string> labels{};
        json parsed = json::parse(query.getColumn("remoteXGMLabels").getString(), nullptr, false);
        if (parsed.is_array()) {
            for (const auto & i : parsed) {
                if (i.is_string()) labels.push_back(i.get<string>());
            }
        }
        attrs.labels = labels;

        results[uid] = attrs;
    }
    query.reset();
    
    return results;
}

uint32_t MailStore::fetchMessageUIDAtDepth(Folder & folder, uint32_t depth, uint32_t before) {
    assertCorrectThread();
    auto & query = _placementStatement("uidAtDepth",
        "SELECT remoteUID FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID < ? AND remoteUID > 0 "
        "ORDER BY remoteUID DESC LIMIT 1 OFFSET ?");
    query.bind(1, folder.accountId());
    query.bind(2, folder.id());
    query.bind(3, before);
    query.bind(4, depth);
    uint32_t result = 1;
    if (query.executeStep()) {
        result = query.getColumn("remoteUID").getUInt();
    }
    query.reset();
    return result;
}

string MailStore::getKeyValue(string key) {
    assertCorrectThread();
    SQLite::Statement query(this->_db, "SELECT value FROM _State WHERE id = ?");
    query.bind(1, key);
    if (query.executeStep()) {
        return query.getColumn(0).getString();
    }
    query.reset();
    return "";
}

void MailStore::saveKeyValue(string key, string value) {
    assertCorrectThread();
    SQLite::Statement query(this->_db, "REPLACE INTO _State (id, value) VALUES (?, ?)");
    query.bind(1, key);
    query.bind(2, value);
    query.exec();
}

vector<shared_ptr<Label>> MailStore::allLabelsCache(string accountId) {
    // todo bg: this assumes a single accountId will ever be used
    if (_labelCacheVersion != globalLabelsVersion) {
        _labelCache = findAll<Label>(Query().equal("accountId", accountId));
        _labelCacheVersion = globalLabelsVersion;
    }
    return _labelCache;
}

const map<string, shared_ptr<Folder>> & MailStore::allFoldersCache(string accountId) {
    // Like allLabelsCache, assumes one accountId per process. Labels are included
    // because Gmail's send path files a message under the Sent label.
    if (_folderCacheVersion != globalFoldersVersion) {
        map<string, shared_ptr<Folder>> next;
        for (auto & folder : findAll<Folder>(Query().equal("accountId", accountId))) {
            next[folder->id()] = folder;
        }
        for (auto & label : findAll<Label>(Query().equal("accountId", accountId))) {
            next[label->id()] = label;
        }
        _folderCache = next;
        _folderCacheVersion = globalFoldersVersion;
    }
    return _folderCache;
}

shared_ptr<Folder> MailStore::folderById(string accountId, string folderId) {
    auto & cache = allFoldersCache(accountId);
    auto it = cache.find(folderId);
    return it == cache.end() ? nullptr : it->second;
}

void MailStore::beginTransaction() {
    assertCorrectThread();
    try {
        _stmtBeginTransaction.exec();
        _stmtBeginTransaction.reset();
        _transactionOpen = true;
    } catch (...) {
        // Always reset the statement so it can be reused, even if exec() failed.
        // This ensures the statement's internal state is consistent for future calls.
        _stmtBeginTransaction.reset();
        throw;
    }
}


void MailStore::rollbackTransaction() {
    // Note: when a transaction is interrupted and we roll it back,
    // running the statement again produces the error again? Unclear...
    _saveUpdateQueries = {};
    _saveInsertQueries = {};
    _removeQueries = {};
    _placementQueries = {};
    // Deltas describe writes that never happened; left in place they would be emitted
    // with the next commit and the client would render rolled-back state.
    _transactionDeltas = {};
    try {
        _stmtRollbackTransaction.exec();
        _stmtRollbackTransaction.reset();
        _transactionOpen = false;
    } catch (...) {
        // Always reset the statement so it can be reused, even if exec() failed.
        // Also clear transaction state since the transaction is no longer valid
        // after a failed rollback attempt.
        _stmtRollbackTransaction.reset();
        _transactionOpen = false;
        throw;
    }
}

// This method allows you to perform work in a transaction and then prevent the
// transaction from emitting any deltas to the client app. If you KNOW the
// transaction is only changing internal data, you can safely do this without the
// client falling out of sync and it can be a performance win in key places where
// many unnecessary updates would cause thrashing on the JS side.
void MailStore::unsafeEraseTransactionDeltas() {
    _transactionDeltas = {};
}

void MailStore::commitTransaction() {
    try {
        _stmtCommitTransaction.exec();
        _stmtCommitTransaction.reset();
    } catch (...) {
        // Always reset the statement so it can be reused, even if exec() failed.
        // Note: We do NOT clear _transactionOpen here because a failed commit
        // may leave the transaction still active (e.g., SQLITE_BUSY allows retry).
        _stmtCommitTransaction.reset();
        throw;
    }

    // emit all of the deltas
    if (_transactionDeltas.size()) {
        SharedDeltaStream()->emit(_transactionDeltas, _streamMaxDelay);
        _transactionDeltas = {};
    }
    _transactionOpen = false;
}

void MailStore::save(MailModel * model) {
    assertCorrectThread();

    model->incrementVersion();
    model->beforeSave(this);

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
        query->clearBindings();
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
        query->clearBindings();
        model->bindToQuery(query.get());
        query->exec();
    }

    model->afterSave(this);

    if (tableName == "Label") {
        globalLabelsVersion += 1;
    }
    if (tableName == "Label" || tableName == "Folder") {
        globalFoldersVersion += 1;
    }

    DeltaStreamItem delta {DELTA_TYPE_PERSIST, model};
    _emit(delta);
}

void MailStore::saveFolderStatus(Folder * folder, json & initialStatus) {
    json & changedStatus = folder->localStatus();
    if (changedStatus == initialStatus) {
        return;
    }

    {
        MailStoreTransaction transaction(this, "saveFolderStatus");
        auto current = find<Folder>(Query().equal("accountId", folder->accountId()).equal("id", folder->id()));
        if (current == nullptr) {
            return;
        }
        for (auto it = changedStatus.begin(); it != changedStatus.end(); ++it) {
            if (initialStatus.count(it.key()) == 0 || initialStatus[it.key()] != it.value()) {
                current->localStatus()[it.key()] = it.value();
            }
        }
        save(current.get());
        transaction.commit();
    }
}

void MailStore::remove(MailModel * model) {
    assertCorrectThread();
    auto tableName = model->tableName();
    if (!_removeQueries.count(tableName)) {
        _removeQueries[tableName] = make_shared<SQLite::Statement>(this->_db, "DELETE FROM " + tableName + " WHERE id = ?");
    }
    auto query = _removeQueries[tableName];
    query->reset();
    query->clearBindings();
    query->bind(1, model->id());
    query->exec();

    model->afterRemove(this);

    if (model->tableName() == "Label") {
        globalLabelsVersion += 1;
    }
    if (model->tableName() == "Label" || model->tableName() == "Folder") {
        globalFoldersVersion += 1;
    }

    DeltaStreamItem delta {DELTA_TYPE_UNPERSIST, model};
    _emit(delta);
}


#pragma mark Placements

SQLite::Statement & MailStore::_placementStatement(const string & key, const string & sql) {
    auto it = _placementQueries.find(key);
    if (it == _placementQueries.end()) {
        it = _placementQueries.emplace(key, make_shared<SQLite::Statement>(_db, sql)).first;
    }
    auto & stmt = *it->second;
    stmt.reset();
    stmt.clearBindings();
    return stmt;
}

// Drains a statement that yields a messageId column and returns the distinct ids in
// first-seen order. Used for UPDATE/DELETE ... RETURNING messageId.
vector<string> MailStore::_collectMessageIds(SQLite::Statement & stmt) {
    vector<string> ids;
    set<string> seen;
    while (stmt.executeStep()) {
        string id = stmt.getColumn(0).getString();
        if (seen.insert(id).second) {
            ids.push_back(id);
        }
    }
    stmt.reset();
    return ids;
}

static const string PLACEMENT_COLUMNS = "rowid, accountId, messageId, folderId, remoteUID, unread, starred, draft, remoteXGMLabels, pendingFolderId";

vector<Placement> MailStore::placementsForMessage(string messageId) {
    assertCorrectThread();
    auto & stmt = _placementStatement("forMessage",
        "SELECT " + PLACEMENT_COLUMNS + " FROM MessageFolder WHERE messageId = ? ORDER BY rowid");
    stmt.bind(1, messageId);
    vector<Placement> results;
    while (stmt.executeStep()) {
        results.push_back(Placement::fromRow(stmt));
    }
    stmt.reset();
    return results;
}

/*
 Rebuilds the message's "folders" snapshot and derived flags from its rows. Called at the
 end of every helper that changed the rows of a message it has in hand, so the snapshot
 is only recomputed for messages whose placements changed.

 - "folders" lists copies keyed by the folder the client should see them in (the pending
   destination during an optimistic move), OR-ing bits when one folder holds several.
 - unread / starred / draft are OR'd over the rows; draft is also true when a copy sits in
   the Drafts folder. "labels" is the union of the rows' X-GM-LABELS (Gmail has one).
 - A message with no rows keeps its flags and labels and is recorded in MessageOrphan: it
   is either in transit to a folder not scanned yet or about to be swept. Any row clears
   the record, which is what keeps a moved message's id when its destination is scanned.
 */
void MailStore::refreshMessageFromPlacements(Message & msg) {
    auto rows = placementsForMessage(msg.id());

    json folders = json::object();
    set<string> labels;
    bool unread = false, starred = false, draft = false;
    for (auto & p : rows) {
        unread = unread || p.unread;
        starred = starred || p.starred;
        draft = draft || p.draft;
        string key = p.reportedFolderId();
        int existing = folders.count(key) ? folders[key].get<int>() : 0;
        folders[key] = existing | p.flagBits();
        for (auto & l : p.labels) {
            labels.insert(l.get<string>());
        }
        auto folder = folderById(msg.accountId(), p.folderId);
        if (folder != nullptr && folder->role() == "drafts") {
            draft = true;
        }
    }

    msg.folders() = folders;
    if (rows.empty()) {
        auto & record = _placementStatement("recordOrphan",
            "INSERT OR IGNORE INTO MessageOrphan (messageId, accountId, since) VALUES (?, ?, ?)");
        record.bind(1, msg.id());
        record.bind(2, msg.accountId());
        record.bind(3, (long long)time(0));
        record.exec();
        record.reset();
        return;
    }

    auto & clear = _placementStatement("clearOrphan", "DELETE FROM MessageOrphan WHERE messageId = ?");
    clear.bind(1, msg.id());
    clear.exec();
    clear.reset();

    msg._data["unread"] = unread;
    msg._data["starred"] = starred;
    msg._data["draft"] = draft;
    msg._data["labels"] = json(vector<string>(labels.begin(), labels.end()));
}

/*
 Records one physical copy at (folder, uid) with the flags the server reported.

 - A row already at (folder, uid) is refreshed in place. If it belonged to a different
   message (a UID the server reused without a UIDVALIDITY change) that message loses the
   copy and its id is returned so the caller can rewrite its snapshot.
 - A row for this message in this folder at UID 0 is a placement whose UID is unknown:
   a local draft, or a copy waiting for a UIDVALIDITY rebuild to relink it. It is
   replaced by the real row so the rebuild converges instead of leaving both.
 */
string MailStore::upsertPlacement(Message & msg, Folder & folder, uint32_t uid, const MessageAttributes & attrs) {
    assertCorrectThread();

    string displacedMessageId;
    if (uid > 0) {
        auto & existing = _placementStatement("upsertExisting",
            "SELECT messageId FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID = ? AND remoteUID > 0");
        existing.bind(1, msg.accountId());
        existing.bind(2, folder.id());
        existing.bind(3, (long long)uid);
        if (existing.executeStep()) {
            string holder = existing.getColumn(0).getString();
            if (holder != msg.id()) {
                displacedMessageId = holder;
            }
        }
        existing.reset();
    } else {
        // UID 0 rows are outside the unique index, so refresh one by hand rather than
        // accumulating a row per save of a local draft.
        auto & unassigned = _placementStatement("upsertUnassigned",
            "UPDATE MessageFolder SET unread = ?, starred = ?, draft = ?, remoteXGMLabels = ?, pendingFolderId = NULL "
            "WHERE messageId = ? AND folderId = ? AND remoteUID = 0");
        unassigned.bind(1, attrs.unread);
        unassigned.bind(2, attrs.starred);
        unassigned.bind(3, attrs.draft);
        unassigned.bind(4, json(attrs.labels).dump());
        unassigned.bind(5, msg.id());
        unassigned.bind(6, folder.id());
        int updated = unassigned.exec();
        unassigned.reset();
        if (updated > 0) {
            refreshMessageFromPlacements(msg);
            return displacedMessageId;
        }
    }

    auto & stmt = _placementStatement("upsert",
        "INSERT INTO MessageFolder (accountId, messageId, folderId, remoteUID, unread, starred, draft, remoteXGMLabels, pendingFolderId) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL) "
        "ON CONFLICT (accountId, folderId, remoteUID) WHERE remoteUID > 0 DO UPDATE SET "
        "messageId = excluded.messageId, unread = excluded.unread, starred = excluded.starred, draft = excluded.draft, "
        "remoteXGMLabels = excluded.remoteXGMLabels, pendingFolderId = NULL");
    stmt.bind(1, msg.accountId());
    stmt.bind(2, msg.id());
    stmt.bind(3, folder.id());
    stmt.bind(4, (long long)uid);
    stmt.bind(5, attrs.unread);
    stmt.bind(6, attrs.starred);
    stmt.bind(7, attrs.draft);
    stmt.bind(8, json(attrs.labels).dump());
    stmt.exec();
    stmt.reset();

    if (uid > 0) {
        auto & relinked = _placementStatement("deleteUnassignedInFolder",
            "DELETE FROM MessageFolder WHERE messageId = ? AND folderId = ? AND remoteUID = 0");
        relinked.bind(1, msg.id());
        relinked.bind(2, folder.id());
        relinked.exec();
        relinked.reset();
    }

    refreshMessageFromPlacements(msg);
    return displacedMessageId;
}

// Gmail keeps one placement per message: All Mail, Spam and Trash are mutually exclusive
// on the server, and a message filed under a Label by the send path is only there until
// All Mail reports it. Rows at UID 0 are local drafts and are left alone.
void MailStore::removePlacementsOutsideFolder(Message & msg, string folderId) {
    assertCorrectThread();
    auto & stmt = _placementStatement("removeOutsideFolder",
        "DELETE FROM MessageFolder WHERE messageId = ? AND folderId != ? AND remoteUID > 0");
    stmt.bind(1, msg.id());
    stmt.bind(2, folderId);
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

// A client flag change fans out to every copy: otherwise the next scan of an untouched
// copy re-derives the old value. Each flag is written on its own so copies that disagree
// on the other flag keep their own value.
void MailStore::setPlacementUnread(Message & msg, bool unread) {
    assertCorrectThread();
    auto & stmt = _placementStatement("setUnreadAll",
        "UPDATE MessageFolder SET unread = ? WHERE messageId = ?");
    stmt.bind(1, unread);
    stmt.bind(2, msg.id());
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

void MailStore::setPlacementStarred(Message & msg, bool starred) {
    assertCorrectThread();
    auto & stmt = _placementStatement("setStarredAll",
        "UPDATE MessageFolder SET starred = ? WHERE messageId = ?");
    stmt.bind(1, starred);
    stmt.bind(2, msg.id());
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

void MailStore::setPlacementLabels(Message & msg, const vector<string> & labels) {
    assertCorrectThread();
    auto & stmt = _placementStatement("setLabels",
        "UPDATE MessageFolder SET remoteXGMLabels = ? WHERE messageId = ?");
    stmt.bind(1, json(labels).dump());
    stmt.bind(2, msg.id());
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

// Optimistic move of one copy: the row keeps its server folder and UID so the remote
// phase can address it, and the snapshot reports it under the destination immediately.
// Keyed by (folder, UID) because two copies in one folder can be bound for different
// folders when an undo spreads them back over their sources.
void MailStore::beginPlacementMove(Message & msg, string fromFolderId, uint32_t uid, string toFolderId) {
    assertCorrectThread();
    auto & stmt = _placementStatement("beginMove",
        "UPDATE MessageFolder SET pendingFolderId = ? WHERE messageId = ? AND folderId = ? AND remoteUID = ?");
    stmt.bind(1, toFolderId);
    stmt.bind(2, msg.id());
    stmt.bind(3, fromFolderId);
    stmt.bind(4, (long long)uid);
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

/*
 Records that the copy at (fromFolderId, oldUid) now sits at (toFolderId, newUid). The
 destination row may already exist, because the destination folder's scan can run
 between the MOVE and this commit:

 - held by this message: the scan recorded the moved copy. The source row is deleted,
   since both describe one server copy.
 - held by another message: a stale row for a UID the server has since reassigned. It
   is deleted so the unique (folder, UID) index admits ours, and that message's id is
   returned so the caller rewrites its snapshot, as after upsertPlacement.

 A move whose source and destination are the same copy is not a move: it settles a copy
 that is already where it belongs, and its only effect is to clear the pending marker.
 It has to be handled before the holder lookup, which would otherwise find this very row,
 treat it as the destination the scan already recorded, and delete it as the source -
 leaving the message with no placement at all.
 */
string MailStore::commitPlacementMove(Message & msg, string fromFolderId, uint32_t oldUid, string toFolderId, uint32_t newUid) {
    assertCorrectThread();

    if (fromFolderId == toFolderId && oldUid == newUid) {
        auto & settle = _placementStatement("commitMoveInPlace",
            "UPDATE MessageFolder SET pendingFolderId = NULL WHERE messageId = ? AND folderId = ? AND remoteUID = ?");
        settle.bind(1, msg.id());
        settle.bind(2, fromFolderId);
        settle.bind(3, (long long)oldUid);
        settle.exec();
        settle.reset();
        refreshMessageFromPlacements(msg);
        return "";
    }

    string holder;
    if (newUid > 0) {
        auto & existing = _placementStatement("commitMoveExisting",
            "SELECT messageId FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID = ? AND remoteUID > 0");
        existing.bind(1, msg.accountId());
        existing.bind(2, toFolderId);
        existing.bind(3, (long long)newUid);
        if (existing.executeStep()) {
            holder = existing.getColumn(0).getString();
        }
        existing.reset();
    }

    if (holder == msg.id()) {
        removePlacement(msg, fromFolderId, oldUid);
        return "";
    }

    if (!holder.empty()) {
        auto & displace = _placementStatement("commitMoveDisplace",
            "DELETE FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID = ?");
        displace.bind(1, msg.accountId());
        displace.bind(2, toFolderId);
        displace.bind(3, (long long)newUid);
        displace.exec();
        displace.reset();
    }

    auto & stmt = _placementStatement("commitMove",
        "UPDATE MessageFolder SET folderId = ?, remoteUID = ?, pendingFolderId = NULL WHERE messageId = ? AND folderId = ? AND remoteUID = ?");
    stmt.bind(1, toFolderId);
    stmt.bind(2, (long long)newUid);
    stmt.bind(3, msg.id());
    stmt.bind(4, fromFolderId);
    stmt.bind(5, (long long)oldUid);
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
    return holder;
}

void MailStore::removePlacement(Message & msg, string folderId, uint32_t uid) {
    assertCorrectThread();
    auto & stmt = _placementStatement("removeOne",
        "DELETE FROM MessageFolder WHERE messageId = ? AND folderId = ? AND remoteUID = ?");
    stmt.bind(1, msg.id());
    stmt.bind(2, folderId);
    stmt.bind(3, (long long)uid);
    stmt.exec();
    stmt.reset();
    refreshMessageFromPlacements(msg);
}

/*
 Bulk helpers: rows only, no Message loaded. Every one that deletes rows records the
 messages it left with none in MessageOrphan in the same transaction, because the caller
 catches the snapshots up in later transactions and nothing but MessageOrphan leads back to
 a message that has no row. UID 0 rows (local drafts, UIDVALIDITY resets) are never
 matched by a range scan because the server never reported them.
 */

void MailStore::_recordOrphansAmong(const vector<string> & messageIds) {
    vector<string> ids = messageIds;
    for (auto chunk : MailUtils::chunksOfVector(ids, 500)) {
        SQLite::Statement stmt(_db,
            "INSERT OR IGNORE INTO MessageOrphan (messageId, accountId, since) "
            "SELECT id, accountId, ? FROM Message WHERE id IN (" + MailUtils::qmarks(chunk.size()) + ") "
            "AND NOT EXISTS (SELECT 1 FROM MessageFolder WHERE MessageFolder.messageId = Message.id)");
        stmt.bind(1, (long long)time(0));
        int idx = 2;
        for (auto & id : chunk) {
            stmt.bind(idx++, id);
        }
        stmt.exec();
    }
}

vector<string> MailStore::deleteVanishedPlacements(Folder & folder, const vector<uint32_t> & uids) {
    assertCorrectThread();
    vector<string> affected;
    vector<uint32_t> all = uids;
    for (auto chunk : MailUtils::chunksOfVector(all, 500)) {
        SQLite::Statement stmt(_db,
            "DELETE FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID > 0 "
            "AND remoteUID IN (" + MailUtils::qmarks(chunk.size()) + ") RETURNING messageId");
        stmt.bind(1, folder.accountId());
        stmt.bind(2, folder.id());
        int idx = 3;
        for (auto uid : chunk) {
            stmt.bind(idx++, (long long)uid);
        }
        auto ids = _collectMessageIds(stmt);
        affected.insert(affected.end(), ids.begin(), ids.end());
    }
    _recordOrphansAmong(affected);
    return affected;
}

// Accepts the folderId / remoteUID queries built by MailUtils::queriesForUIDRangesInIndexSet,
// which may describe an open-ended range like 12:* that cannot be expanded to a list.
vector<string> MailStore::deleteVanishedPlacements(Folder & folder, Query & uidQuery) {
    assertCorrectThread();
    string clauses = uidQuery.getSQL();
    const string wherePrefix = " WHERE ";
    if (clauses.compare(0, wherePrefix.size(), wherePrefix) != 0) {
        throw SyncException("query-builder", "deleteVanishedPlacements requires a query with clauses", false);
    }
    clauses = clauses.substr(wherePrefix.size());

    SQLite::Statement stmt(_db,
        "DELETE FROM MessageFolder WHERE accountId = ?1 AND remoteUID > 0 AND (" + clauses + ") RETURNING messageId");
    stmt.bind(1, folder.accountId());
    uidQuery.bind(stmt, 2);
    auto affected = _collectMessageIds(stmt);
    _recordOrphansAmong(affected);
    return affected;
}

// UIDVALIDITY changed: every UID in the folder is meaningless. Rows stay at UID 0 (still
// visible, still counted) until the rebuild assigns new UIDs. Nothing to emit.
// The `remoteUID > 0` term is what lets the partial MessageFolderUIDIndex serve this.
void MailStore::resetPlacementUIDs(Folder & folder) {
    assertCorrectThread();
    auto & stmt = _placementStatement("resetUIDs",
        "UPDATE MessageFolder SET remoteUID = 0 WHERE accountId = ? AND folderId = ? AND remoteUID > 0");
    stmt.bind(1, folder.accountId());
    stmt.bind(2, folder.id());
    stmt.exec();
    stmt.reset();
}

// After a UIDVALIDITY rebuild has visited every UID in the folder, a row still at UID 0 is
// a copy the server no longer has. Draft rows are exempt: a local draft sits at UID 0 by
// design until it is sent.
vector<string> MailStore::deleteUnassignedPlacements(Folder & folder) {
    assertCorrectThread();
    auto & stmt = _placementStatement("deleteUnassigned",
        "DELETE FROM MessageFolder WHERE accountId = ? AND folderId = ? AND remoteUID = 0 AND draft = 0 RETURNING messageId");
    stmt.bind(1, folder.accountId());
    stmt.bind(2, folder.id());
    auto affected = _collectMessageIds(stmt);
    _recordOrphansAmong(affected);
    return affected;
}

vector<string> MailStore::orphanMessageIdsBefore(string accountId, time_t before) {
    assertCorrectThread();
    auto & stmt = _placementStatement("orphansBefore",
        "SELECT messageId FROM MessageOrphan WHERE accountId = ? AND since < ?");
    stmt.bind(1, accountId);
    stmt.bind(2, (long long)before);
    return _collectMessageIds(stmt);
}

// Called from Message::afterRemove, so the orphan record goes with the message.
void MailStore::deletePlacementsForMessage(string messageId) {
    assertCorrectThread();
    auto & stmt = _placementStatement("deleteForMessage", "DELETE FROM MessageFolder WHERE messageId = ?");
    stmt.bind(1, messageId);
    stmt.exec();
    stmt.reset();
    auto & orphan = _placementStatement("deleteOrphanForMessage", "DELETE FROM MessageOrphan WHERE messageId = ?");
    orphan.bind(1, messageId);
    orphan.exec();
    orphan.reset();
}

// The ids the folder's deletion will affect, including messages only pointed at it by a
// move still in flight, so the caller can detach them in bounded transactions.
vector<string> MailStore::messageIdsWithPlacementsInFolder(string folderId) {
    assertCorrectThread();
    auto & stmt = _placementStatement("messagesInFolder",
        "SELECT DISTINCT messageId FROM MessageFolder WHERE folderId = ? OR pendingFolderId = ?");
    stmt.bind(1, folderId);
    stmt.bind(2, folderId);
    return _collectMessageIds(stmt);
}

void MailStore::deletePlacementsForFolder(string folderId, const vector<string> & messageIds) {
    assertCorrectThread();
    if (messageIds.empty()) {
        return;
    }
    string inList = " AND messageId IN (" + MailUtils::qmarks(messageIds.size()) + ")";
    SQLite::Statement abandon(_db, "UPDATE MessageFolder SET pendingFolderId = NULL WHERE pendingFolderId = ?" + inList);
    SQLite::Statement remove(_db, "DELETE FROM MessageFolder WHERE folderId = ?" + inList);
    for (auto stmt : {&abandon, &remove}) {
        stmt->bind(1, folderId);
        int idx = 2;
        for (auto & id : messageIds) {
            stmt->bind(idx++, id);
        }
        stmt->exec();
    }
    _recordOrphansAmong(messageIds);
}

// A move still in flight towards the deleted folder is abandoned: the copy stays where
// the server has it.
vector<string> MailStore::deletePlacementsForFolder(string folderId) {
    assertCorrectThread();
    auto & abandon = _placementStatement("abandonMovesToFolder",
        "UPDATE MessageFolder SET pendingFolderId = NULL WHERE pendingFolderId = ? RETURNING messageId");
    abandon.bind(1, folderId);
    vector<string> affected = _collectMessageIds(abandon);

    auto & stmt = _placementStatement("deleteForFolder",
        "DELETE FROM MessageFolder WHERE folderId = ? RETURNING messageId");
    stmt.bind(1, folderId);
    for (auto & id : _collectMessageIds(stmt)) {
        if (std::find(affected.begin(), affected.end(), id) == affected.end()) {
            affected.push_back(id);
        }
    }
    _recordOrphansAmong(affected);
    return affected;
}

void MailStore::_emit(DeltaStreamItem & delta) {
    if (_transactionOpen) {
        _transactionDeltas.push_back(delta);
    } else {
        SharedDeltaStream()->emit(delta, _streamMaxDelay);
    }
}

shared_ptr<MailModel> MailStore::findGeneric(string type, Query query) {
    assertCorrectThread();
    transform(type.begin(), type.end(), type.begin(), ::tolower);

    if (type == "message") {
        return find<Message>(query);
    } else if (type == "thread") {
        return find<Thread>(query);
    } else if (type == "contact") {
        return find<Contact>(query);
    }
    assert(false);
}

vector<shared_ptr<MailModel>> MailStore::findAllGeneric(string type, Query query) {
    assertCorrectThread();
    transform(type.begin(), type.end(), type.begin(), ::tolower);

    if (type == "message") {
        auto results = findAll<Message>(query);
        std::vector<std::shared_ptr<MailModel>> baseResults(results.begin(), results.end());
        return baseResults;
    } else if (type == "thread") {
        auto results = findAll<Thread>(query);
        std::vector<std::shared_ptr<MailModel>> baseResults(results.begin(), results.end());
        return baseResults;
    } else if (type == "contact") {
        auto results = findAll<Contact>(query);
        std::vector<std::shared_ptr<MailModel>> baseResults(results.begin(), results.end());
        return baseResults;
    }
    assert(false);
}

vector<Metadata> MailStore::findAndDeleteDetachedPluginMetadata(string accountId, string objectId) {
    assertCorrectThread();
    if (!_saveInsertQueries.count("metadata")) {
        auto stmt = make_shared<SQLite::Statement>(db(), "SELECT version, value, pluginId, objectType FROM DetatchedPluginMetadata WHERE objectId = ? AND accountId = ?");
        _saveInsertQueries["metadata"] = stmt;
    }

    vector<Metadata> results;
    auto st = _saveInsertQueries["metadata"];
    st->reset();
    st->clearBindings();
    st->bind(1, objectId);
    st->bind(2, accountId);
    while (st->executeStep()) {
        Metadata m;
        m.accountId = accountId;
        m.version = st->getColumn("version").getInt();
        m.value = json::parse(st->getColumn("value").getString());
        m.pluginId = st->getColumn("pluginId").getString();
        m.objectType = st->getColumn("objectType").getString();
        m.objectId = objectId;
        results.push_back(m);
    }
    if (results.size()) {
        SQLite::Statement dt(db(), "DELETE FROM DetatchedPluginMetadata WHERE objectId = ? AND accountId = ?");
        dt.bind(1, objectId);
        dt.bind(2, accountId);
        dt.exec();
    }
    return results;
}

void MailStore::saveDetachedPluginMetadata(Metadata & m) {
    assertCorrectThread();
    SQLite::Statement st(db(), "REPLACE INTO DetatchedPluginMetadata (objectId, objectType, accountId, pluginId, value, version) VALUES (?,?,?,?,?,?)");
    st.bind(1, m.objectId);
    st.bind(2, m.objectType);
    st.bind(3, m.accountId);
    st.bind(4, m.pluginId);
    st.bind(5, m.value.dump());
    st.bind(6, m.version);
    st.exec();
}

void MailStore::setStreamDelay(int streamMaxDelay) {
    _streamMaxDelay = streamMaxDelay;
}


