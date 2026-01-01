# Code Review: MailStore

## Overview

`MailStore` is the SQLite database layer responsible for persisting all email data. It uses a "fat rows" pattern with a JSON `data` column plus indexed columns for queryable fields. All model changes flow through MailStore and are emitted as deltas to the DeltaStream.

### Key Responsibilities

1. **Database Operations**: Save, find, remove models with prepared statement caching
2. **Transaction Management**: RAII transactions with delta batching
3. **Delta Emission**: Queue changes for client notification via DeltaStream
4. **Thread Safety**: Enforces single-thread access per MailStore instance
5. **Schema Migration**: Handles database version upgrades
6. **Label Caching**: Maintains an in-memory cache of Gmail labels

### Usage Patterns

- Each sync worker thread has its own MailStore instance
- Main thread has a separate MailStore instance
- All model CRUD operations go through MailStore
- Transactions batch deltas until commit

---

## Critical Issues

### 1. JSON Parse Without Exception Handling (MailStore.cpp:244)

```cpp
for (const auto i : json::parse(query.getColumn("remoteXGMLabels").getString())) {
    labels.push_back(i.get<string>());
}
```

**Problem**: If `remoteXGMLabels` column contains malformed JSON (from database corruption or bugs), `json::parse()` will throw an unhandled exception.

**Impact**: Crash during message attribute fetching.

**Location**: `MailStore.cpp:244`

---

### 2. Global Atomic Accessed Without Memory Ordering (MailStore.cpp:25)

```cpp
std::atomic<int> globalLabelsVersion {1};
// ...
globalLabelsVersion += 1;  // Uses default memory ordering
```

**Problem**: The atomic uses default (sequentially consistent) memory ordering which is correct but expensive. More concerning, the cache check `_labelCacheVersion != globalLabelsVersion` could have race conditions if multiple threads save Labels simultaneously.

**Impact**: Potential cache inconsistency, though mostly theoretical.

**Location**: `MailStore.cpp:25, 389, 432, 290-293`

---

### 3. Prepared Statement Cache Not Cleared on Error (MailStore.cpp:361-363, 380-383)

```cpp
auto query = _saveUpdateQueries[tableName];
query->reset();
model->bindToQuery(query.get());
query->exec();
```

**Problem**: If `exec()` throws an exception, the prepared statement remains bound with the previous parameters. On next use, `reset()` is called but bindings from the failed attempt might cause issues.

**Impact**: Potential data corruption or incorrect SQL operations after an error.

**Location**: `MailStore.cpp:361-363, 380-383`

---

### 4. Transaction State Not Reset on SQLite Exception (MailStore.cpp:297-302)

```cpp
void MailStore::beginTransaction() {
    assertCorrectThread();
    _stmtBeginTransaction.exec();
    _stmtBeginTransaction.reset();
    _transactionOpen = true;  // Set even if exec() succeeds
}
```

**Problem**: If `_stmtBeginTransaction.exec()` throws, `_transactionOpen` is not set to true, which is correct. But if the statement partially executes before failing, the SQLite state may be inconsistent with our tracked state.

**Impact**: Inconsistent transaction state tracking.

**Location**: `MailStore.cpp:297-302`

---

### 5. `assert(false)` in Production Code (MailStore.cpp:458, 478)

```cpp
shared_ptr<MailModel> MailStore::findGeneric(string type, Query query) {
    // ...
    assert(false);  // If type doesn't match
}
```

**Problem**: `assert()` is typically disabled in release builds (`NDEBUG`). If an unknown type is passed, the function would fall through with undefined behavior rather than crashing cleanly.

**Impact**: Undefined behavior in release builds if unknown type is passed.

**Location**: `MailStore.cpp:458, 478`

---

## High Severity Issues

### 6. Unbounded Delta Accumulation (MailStore.cpp:440-441)

```cpp
void MailStore::_emit(DeltaStreamItem & delta) {
    if (_transactionOpen) {
        _transactionDeltas.push_back(delta);  // No limit
    } else {
        SharedDeltaStream()->emit(delta, _streamMaxDelay);
    }
}
```

**Problem**: During large transactions (e.g., syncing thousands of messages), `_transactionDeltas` grows without bound.

**Impact**: Memory exhaustion during bulk operations.

**Location**: `MailStore.cpp:440-441`

---

### 7. VACUUM Can Fail Silently During Migration (MailStore.cpp:174-182)

```cpp
try {
    SQLite::Statement(_db, "VACUUM").exec();
} catch (std::exception & ex) {
    cout << "\n" << "Vacuuming failed with SQLite error:";
    cout << "\n" << ex.what();
    // Returns normally, allowing app to launch
}
```

**Problem**: While intentional, VACUUM failures during migration are only logged to stdout (which the client might not see). This could mask disk space issues.

**Impact**: Silent failure, potential database bloat.

**Location**: `MailStore.cpp:174-182`

---

### 8. Range Calculation Overflow (MailStore.cpp:228-232)

```cpp
if (range.length == UINT64_MAX) {
    query.bind(4, LLONG_MAX);
} else {
    query.bind(4, (long long)(range.location + range.length));  // Potential overflow
}
```

**Problem**: If `range.location + range.length` exceeds `LLONG_MAX` (but is less than `UINT64_MAX`), this could overflow.

**Impact**: Incorrect message range queries.

**Location**: `MailStore.cpp:231`

---

### 9. Label Cache Assumes Single Account (MailStore.cpp:288-295)

```cpp
vector<shared_ptr<Label>> MailStore::allLabelsCache(string accountId) {
    // todo bg: this assumes a single accountId will ever be used
    if (_labelCacheVersion != globalLabelsVersion) {
        _labelCache = findAll<Label>(Query().equal("accountId", accountId));
        _labelCacheVersion = globalLabelsVersion;
    }
    return _labelCache;
}
```

**Problem**: The comment acknowledges this limitation. The cache stores labels for one account but the method takes an `accountId` parameter, suggesting it might be called with different accounts.

**Impact**: Incorrect labels returned if called with different account IDs.

**Location**: `MailStore.cpp:288-295`

---

### 10. Missing Null Check in saveFolderStatus (MailStore.cpp:404-406)

```cpp
auto current = find<Folder>(Query().equal("accountId", folder->accountId()).equal("id", folder->id()));
if (current == nullptr) {
    return;  // Silently returns without saving
}
```

**Problem**: If the folder doesn't exist, the method silently returns. The caller won't know the save failed.

**Impact**: Silent data loss if folder is deleted during sync.

**Location**: `MailStore.cpp:404-406`

---

## Medium Severity Issues

### 11. Metadata Query Uses Wrong Cache Map (MailStore.cpp:483-486)

```cpp
void MailStore::findAndDeleteDetatchedPluginMetadata(string accountId, string objectId) {
    if (!_saveInsertQueries.count("metadata")) {
        auto stmt = make_shared<SQLite::Statement>(db(), "SELECT ...");
        _saveInsertQueries["metadata"] = stmt;  // Stored in INSERT cache but it's a SELECT
    }
}
```

**Problem**: A SELECT statement is being cached in `_saveInsertQueries`. While this works, it's confusing and could cause issues if the cache is cleared differently for inserts vs selects.

**Impact**: Code maintainability, potential subtle bugs if caches are cleared differently.

**Location**: `MailStore.cpp:483-486`

---

### 12. Missing Thread Safety for `_streamMaxDelay` (MailStore.cpp:524-526)

```cpp
void MailStore::setStreamDelay(int streamMaxDelay) {
    _streamMaxDelay = streamMaxDelay;
}
```

**Problem**: `_streamMaxDelay` is a plain `int` that could be read in `_emit()` while being written by another thread.

**Impact**: Potential torn reads on some architectures.

**Location**: `MailStore.cpp:524-526`

---

### 13. Synchronous PRAGMA Might Not Be Applied (MailStore.cpp:97-100)

```cpp
SQLite::Statement(_db, "PRAGMA journal_mode = WAL").executeStep();
SQLite::Statement(_db, "PRAGMA main.page_size = 4096").exec();
```

**Problem**: `page_size` can only be changed on an empty database. On an existing database, this PRAGMA is silently ignored.

**Impact**: Suboptimal performance if database was created with different page size.

**Location**: `MailStore.cpp:98`

---

### 14. No Validation on Key-Value Store (MailStore.cpp:269-286)

```cpp
string MailStore::getKeyValue(string key) {
    // No validation of key
}

void MailStore::saveKeyValue(string key, string value) {
    // No validation of key or value
}
```

**Problem**: No input validation could allow SQL injection if keys/values contain unexpected characters (though parameterized queries prevent actual injection).

**Impact**: Low risk, but could cause issues with special characters.

**Location**: `MailStore.cpp:269-286`

---

### 15. Template Query Without LIMIT (MailStore.hpp:123-132)

```cpp
template<typename ModelClass>
shared_ptr<ModelClass> find(Query & query) {
    SQLite::Statement statement(this->_db, "SELECT data FROM " + ModelClass::TABLE_NAME + query.getSQL() + " LIMIT 1");
```

**Problem**: The `find()` template adds `LIMIT 1`, but `findAll()` only adds a limit if `query.getLimit() != 0`. Queries without a limit could return huge result sets.

**Impact**: Memory exhaustion on unbounded queries.

**Location**: `MailStore.hpp:134-150`

---

## Low Severity / Code Quality Issues

### 16. Inconsistent Error Handling

Some methods throw exceptions, others return null/empty values, and others use `assert()`. This inconsistency makes error handling difficult to reason about.

---

### 17. Copy in Template Loop (MailStore.hpp:163)

```cpp
for (auto chunk : chunks) {  // Copies each chunk
```

**Problem**: `chunk` is copied on each iteration.

**Impact**: Minor performance overhead.

**Recommendation**: Use `const auto& chunk`.

---

### 18. Typo: "Detatched" (MailStore.cpp:481, 512)

```cpp
vector<Metadata> MailStore::findAndDeleteDetatchedPluginMetadata(...)
void MailStore::saveDetatchedPluginMetadata(Metadata & m)
```

**Problem**: "Detatched" should be "Detached".

**Location**: `MailStore.cpp:481, 512`

---

### 19. Missing `_transactionOpen` Initialization (MailStore.hpp:62)

```cpp
bool _transactionOpen;
```

**Problem**: `_transactionOpen` is not explicitly initialized in the constructor initializer list. It should be `false` initially.

**Impact**: Undefined initial value (though in practice likely zero-initialized due to class storage).

**Location**: `MailStore.hpp:62`

---

### 20. Database Busy Timeout May Be Insufficient (MailStore.cpp:90)

```cpp
_db.setBusyTimeout(10 * 1000);  // 10 seconds
```

**Problem**: 10 seconds might not be enough during heavy sync operations or when the main thread is blocked.

**Impact**: `SQLITE_BUSY` errors under load.

**Location**: `MailStore.cpp:90`

---

## Recommendations

### Immediate Fixes

1. **Add try-catch around JSON parsing**:
   ```cpp
   try {
       for (const auto i : json::parse(query.getColumn("remoteXGMLabels").getString())) {
           labels.push_back(i.get<string>());
       }
   } catch (const json::parse_error& e) {
       logger->error("Failed to parse labels: {}", e.what());
   }
   ```

2. **Replace `assert(false)` with proper exceptions**:
   ```cpp
   throw SyncException("unknown-type", "Unknown model type: " + type, false);
   ```

3. **Initialize `_transactionOpen` to `false`** in the constructor.

4. **Add bounds checking** to delta accumulation with periodic flushing.

5. **Fix label cache** to properly handle multiple accounts or enforce single-account usage.

### Architecture Improvements

1. Add a maximum transaction delta count with automatic intermediate commits.

2. Consider using a connection pool pattern for thread safety rather than per-thread instances.

3. Add proper logging for all error paths instead of silent returns.

4. Implement proper prepared statement cleanup on errors.

### Testing Recommendations

1. Test with malformed JSON in database columns.
2. Stress test with very large transactions (>10,000 records).
3. Test concurrent Label saves from multiple threads.
4. Test database access patterns under low disk space conditions.
5. Test with databases created by older versions (different page sizes).

---

## Summary

The MailStore class is generally well-structured but has several potential issues:

1. **JSON parsing without exception handling** - could crash on corrupted data
2. **Unbounded delta accumulation** - memory exhaustion risk
3. **Label cache single-account assumption** - documented but dangerous
4. **Missing initializers and improper error handling** - undefined behavior risks

The thread safety model (one MailStore per thread) is sound, but the global `globalLabelsVersion` atomic and shared `_streamMaxDelay` variable could cause subtle issues.
