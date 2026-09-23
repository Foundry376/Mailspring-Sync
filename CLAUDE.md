# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Mailspring-Sync is the native C++17 sync engine for the Mailspring email client. It handles email, contact, and calendar synchronization via IMAP/SMTP using MailCore2, storing data in SQLite with a JSON-based schema.

## Build Commands

### Linux
```bash
# Install dependencies (Ubuntu)
sudo apt install libc-ares-dev libicu-dev libctemplate-dev libtidy-dev uuid-dev libxml2-dev libsasl2-dev liblzma-dev libcurl4-openssl-dev libglib2.0-dev libssl-dev

# Build (from project root)
cd Vendor/libetpan && ./autogen.sh && make && sudo make install prefix=/usr
cd Vendor/mailcore2 && mkdir -p build && cd build && cmake .. && make
cmake . && make
```

### macOS
```bash
xcodebuild -scheme mailsync -configuration Release
```

### Windows

Windows builds use [vcpkg](https://vcpkg.io/) for dependency management. Dependencies are defined in `vcpkg.json` at the project root.

**Local Development:**
```cmd
# Install vcpkg (one-time setup)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
set VCPKG_ROOT=C:\vcpkg

# Install dependencies (from project root)
vcpkg install --triplet x86-windows

# Build
cd Windows
msbuild.exe mailsync.sln /property:Configuration=Release;Platform=Win32
```

**CI/CD:** GitHub Actions automatically installs vcpkg dependencies during the build (see `.github/workflows/build-windows.yml`).

**vcpkg-managed dependencies:** openssl, curl, libxml2, zlib, icu, libiconv, tidy-html5, ctemplate, pthreads, cyrus-sasl

## Running Mailsync

Requires environment variables and command-line arguments:
```bash
CONFIG_DIR_PATH=/path/to/config IDENTITY_SERVER=https://id.getmailspring.com \
  ./mailsync --identity "<identity-json>" --account "<account-json>" --mode sync
```

Modes: `sync`, `test` (auth validation), `reset` (clear account data), `migrate` (run migrations)

For debugging in Xcode/Visual Studio, configure the debugger to pass `--identity`, `--account`, and `--mode` arguments.

## Architecture

### Reactive Data Flow (Core Path)

All database changes flow through an entity layer and are emitted as a JSON event stream to stdout, enabling the Mailspring UI to reactively update.

**Data Flow:**
1. **Model modification** → Caller modifies a `MailModel` subclass (Message, Thread, Folder, etc.)
2. **Save via MailStore** → `store->save(model)` increments version, calls `beforeSave()`, writes to SQLite, calls `afterSave()`
3. **Delta creation** → Save/remove creates a `DeltaStreamItem` with type "persist" or "unpersist"
4. **Delta emission** → `_emit()` queues the delta (immediately or within transaction)
5. **Output to stdout** → `DeltaStream` flushes buffered deltas as newline-separated JSON

**Transaction Behavior:**
```cpp
MailStoreTransaction transaction(store, "operationName");
store->save(model1);  // Delta accumulated
store->save(model2);  // Delta accumulated
transaction.commit(); // All deltas emitted together
// Destructor auto-rollbacks if commit() not called
```

**Delta Coalescing:** Multiple saves of the same object within a flush window are merged—only the final state is emitted, with keys merged to preserve conditionally-included fields (e.g., `message.body`).

**Output Format:**
```json
{"type":"persist","modelClass":"Message","modelJSONs":[{...}]}
{"type":"unpersist","modelClass":"Thread","modelJSONs":[{"id":"..."}]}
```

**Key Classes:**
- `MailModel` (`MailSync/Models/MailModel.hpp`) - Base class with `_data` JSON, lifecycle hooks, version tracking
- `MailStore` (`MailSync/MailStore.hpp`) - Database layer, emits deltas on save/remove
- `MailStoreTransaction` - RAII transaction wrapper, batches deltas until commit
- `DeltaStream` (`MailSync/DeltaStream.hpp`) - Singleton (`SharedDeltaStream()`) managing stdout output with buffering

### Process Model
Each mailsync process handles a single email account. Mailspring runs one process per connected account. The process communicates via:
- **stdout**: Emits newline-separated JSON for all model changes (see Reactive Data Flow above)
- **stdin**: Accepts JSON task objects and commands (queue-task, cancel-task, wake-workers, need-bodies)

### Threading Model
- **Main thread**: Listens on stdin for tasks and commands
- **Background thread** (`SyncWorker`): Iterates folders, performs incremental sync using CONDSTORE/QRESYNC, and runs the end-of-pass orphan sweep
- **Foreground thread** (`SyncWorker`): IDLEs on primary folder, handles body fetches and task execution
- **CalContacts thread** (`DAVWorker`, `GoogleContactsWorker`): Calendar/contact sync via CardDAV/CalDAV
- **Metadata threads** (`MetadataWorker`, `MetadataExpirationWorker`): Syncs plugin metadata to/from id.getmailspring.com

### Key Components
- `MailStore`: SQLite database wrapper with template-based queries. Uses "fat" rows with a `data` JSON column plus indexed columns for queryable fields. See Reactive Data Flow above.
- `TaskProcessor`: Handles local (immediate) and remote (network) task execution for operations like sending mail, modifying flags, etc.
- `MailProcessor`: Parses IMAP messages, creates stable IDs from headers, upserts placements on ingest, deletes vanished copies and sweeps expired orphans (see Message Identity and Placements)

### Models (in `MailSync/Models/`)
Account, Message, Thread, Folder, Label, Contact, ContactBook, ContactGroup, Calendar, Event, File, Task, Identity

`Placement` (`MailSync/Placement.hpp`) is a plain struct over a `MessageFolder` row, not a `MailModel`: it is never streamed to the client on its own.

### Message Identity and Placements

A `Message` is the *logical* message: its `id` is a deterministic hash of headers
(`MailUtils::idForMessage` — account, date, subject, sorted recipients, Message-ID; the
folder is deliberately not part of it). That is what lets the engine recognise a message
after it moves and keep plugin metadata (snooze, reminders, open/link tracking) attached
to the same id on every device.

A **placement** is one physical copy of that message on the server: a `MessageFolder` row
`(accountId, messageId, folderId, remoteUID, unread, starred, draft, remoteXGMLabels,
pendingFolderId)`, unique on `(accountId, folderId, remoteUID)` for
`remoteUID > 0`. A message has one or more placements, because the same message legitimately
exists in several folders at once — a self-addressed mail is delivered to Inbox by SMTP and
saved to Sent by the client on every provider; Exchange stores duplicate Sent copies at
adjacent UIDs; ProtonMail Bridge exposes `Labels/*` folders; iCloud and NetEase duplicate
freely. Two copies in the same folder are two placements. `remoteUID = 0` means "not on the
server" (a local draft, or a row awaiting relink after a UIDVALIDITY change).

**Invariants — read these before touching anything that writes a message's location:**

- **The `MessageFolder` table is canonical.** `Message._data["folders"]` is a snapshot map
  `{ "<folderId>": bits }` (`1` unread, `2` starred, `4` draft, per copy) plus derived
  message-level `unread`/`starred`/`draft` (OR over placements; `draft` is also true for
  any copy in a `drafts`-role folder). The snapshot exists so the Thread refcount diff and
  the client never need a join. It carries folder ids only — no path or role; resolve those
  through `MailStore::folderById`.
- **The snapshot is rebuilt from the rows when the message is saved.** Every row change
  goes through a `MailStore` placement helper (`upsertPlacement`,
  `setPlacementUnread/Starred/Labels`, `beginPlacementMove` / `commitPlacementMove`,
  `removePlacement`; bulk SQL-only `deleteVanishedPlacements`, `resetPlacementUIDs`,
  `deleteUnassignedPlacements`, `deletePlacementsForMessage/Folder`). A per-message helper
  sets `Message::_placementsChanged`, and `Message::beforeSave` then runs
  `refreshMessageFromPlacements` (one indexed query, plus `MessageOrphan` bookkeeping) once
  however many helpers ran; saves that touched no rows run no query. Callers that must see
  the result before deciding whether to save (`updateMessage`, `insertMessage`'s thread
  diff) call `refreshMessageFromPlacements` themselves, which clears the flag. Bulk helpers
  return the affected message ids; the caller loads, refreshes and saves those. Do not write
  `MessageFolder` or `folders` anywhere else, and save every message a helper marked. After every scenario the test harness (`test/harness/invariants.py`)
  recomputes each derived layer from the one below it and fails on any difference:
  `folders`/`labels`/flags from `MessageFolder`, thread `_refs`/`_u` and counters from the
  message snapshots, `ThreadCategory` from the thread arrays, `ThreadCounts` from
  `ThreadCategory`, and `MessageOrphan` against the messages with no `MessageFolder` row.
- **A move never deletes the `Message`.** When a folder scan (range diff, QRESYNC VANISHED,
  untagged EXPUNGE) or a UID taken over by another message finds a copy gone, its row is
  deleted, the folder key is dropped from the snapshot and the message saved, so the client
  sees the copy leave that folder within seconds. A message left with no row keeps its
  flags and is recorded in `MessageOrphan (messageId, accountId, since)` by the same helper,
  in the same transaction; `refreshMessageFromPlacements` records and clears it, so any
  later upsert of a copy clears it. At the end of every background `syncNow` pass,
  `MailProcessor::sweepExpiredOrphans(sweepBefore)` removes the messages orphaned before
  `sweepBefore` through `store->remove`, so `Message::afterRemove` fixes the thread, body,
  metadata and orphan record. `sweepBefore` is the pass start, lowered for each folder the
  pass did not cover in full (STATUS failed, still in initial sync, a truncated fetch or
  rebuild, a failed gap scan) to the start of the last pass that did (kept in memory,
  `SyncWorker::folderCoveredAt`; never covered since launch counts as 0) — but never below
  `ORPHAN_SWEEP_MAX_WAIT` (24h; env `ORPHAN_SWEEP_MAX_WAIT` overrides) ago, so one folder
  that is never covered delays the sweep instead of disabling it. A folder whose initial
  walk moved down during the pass is exempt from that floor and holds the bound at its
  last coverage however long the walk takes; a walk that stops moving falls back to the
  floor. Skipped `\All` folders
  count as covered. Deleting a folder (or ExpungeAllInFolder) removes the messages
  whose only copy was there right away. A plain-IMAP MOVE seen
  source-first therefore streams `persist {folders: {}}` then `persist {folders: {dest}}`
  on the same id — never `unpersist` — and metadata survives. Both workers share this
  grace rule because it is keyed on the pass timestamp, not on a per-worker phase.
- **The `syncedAt` guard is message-level.** While `Message._sa` is in the future (now + 24h
  from a task's local phase; `_suc` counts the tasks holding it, and each task's remote
  phase releases its hold, whether it succeeds or fails), `updateMessage` ignores server data for copies it has already
  recorded. This is what stops a scan of the *source* folder from
  resurrecting a copy the user just moved away or reverting a flag the user just changed;
  after the local phase the source placement is the thing in flight, so a per-placement
  timestamp would have nowhere to live. A copy at a (folder, UID) the message has no row for
  is always recorded, with the server's flags: another client may have moved the message's
  only copy, and skipping it would leave the message to the orphan sweep. A scan never
  clears a row's `pendingFolderId`; only the task's commit or its failure does.
- **Optimistic moves are pending placements.** A task's local phase sets
  `pendingFolderId` on the placements it decided to move; the snapshot reports them under
  the pending folder, so the client updates immediately. The remote phase MOVEs (or
  COPY + `\Deleted` + EXPUNGE without MOVE) per source folder and rewrites each row in
  place with the new UID. A copy is moved even when the destination already holds one
  (two copies in one folder are two placements). `ChangeFolderTask` carries
  `sourceFolderIds[]` from the client (destination role trash/spam → every placement;
  otherwise the listed folders, or every non-sent/drafts placement when empty), and the
  engine writes `undoPlacements` (`{ messageId: [folderId per moved copy] }`) into the
  task during its local phase. The undo task carries it as `restorePlacements` with
  `sourceFolderIds = [original destination]` and moves that many of the message's copies
  in the destination back, one to each recorded folder (copies still in flight first,
  then the highest UIDs). A later task's pending marker survives an earlier task's
  commit, so an undo queued before the move reached the server still lands. When a
  folder's MOVE fails, the copies already moved are committed and the task's markers on
  the rest are dropped, so they show where the server has them.
- **Flags fan out.** Mark-read / star set every placement locally and STORE in every
  folder that holds a copy; per-folder thread unread counts use the copy's own bit, so a
  message unread in Inbox and read in Sent bolds the thread in Inbox only.
- **Gmail keeps exactly one placement** (All Mail, Spam or Trash) with X-GM-LABELS on it;
  a placement upserted into one of those three removes the other two (Gmail has no QRESYNC
  to VANISH the old copy promptly). The send path finds the sent message in All Mail and
  creates the placement there — a Label is never a placement folder. ProtonMail's `\All`
  mailbox is still skipped (`isDuplicateAllMail`): placements would make it safe, but it
  doubles every scan and a delete from All Mail on Bridge is a delete everywhere.
- **UIDVALIDITY** resets the folder's placements to `remoteUID = 0` silently (no deltas),
  the heavy `1:*` rebuild relinks `(messageId, folderId)` rows in place, and whatever is
  still at UID 0 when the rebuild completes is deleted like a vanished copy — except drafts.

The design, rationale and migration are in
`../docs/message-placements-plan.md` (client repo). Schema version 10 introduced
`MessageFolder` and `MessageOrphan` and rebuilt `Message` without `remoteFolderId`/`remoteUID`/
`remoteXGMLabels`; downgrading past V10 is unsupported (the remedy is Rebuild database —
the local store is an IMAP cache).

### Vendor Libraries
Located in `Vendor/` - these are built from source and some contain local modifications:
- **libetpan** - IMAP/SMTP library (modified from upstream)
- **mailcore2** - High-level mail library (modified from upstream)
- **SQLiteCpp** - SQLite C++ wrapper
- **nlohmann-json** - JSON library (header-only)
- **spdlog** - Logging library
- **icalendarlib** - iCalendar parsing
- **StanfordCPPLib** - Utility library

On Windows, external binary dependencies (OpenSSL, curl, libxml2, etc.) are managed via vcpkg rather than vendored binaries.

## Comment Style

Write for a technical reader who prefers self-documenting code. Prefer clearer names and
smaller functions over commentary; a comment that restates the code should be deleted.

- State **current behavior and rationale** — why the code is the way it is, and what a
  reader would otherwise get wrong. Never narrate thought process, stream of
  consciousness, or the path you took to the answer.
- Never describe history: where code used to live, what it was folded out of, what the
  previous implementation did, or what a diff changed. Git records that.
- Cite external evidence when it exists — a Sentry issue ID (`MAILSPRING-CLIENT-AC`), an
  upstream commit or PR in mailcore2/libetpan/Electron, a spec section, a provider quirk.
  These justify code that otherwise looks arbitrary and are the most valuable comments in
  the codebase.
- Paragraph-length comments should be rare, and almost always attach to a function, class,
  or module rather than sitting inline. Inline comments belong on one non-obvious line and
  should be one or two lines long.

## Coding Conventions

These are the conventions the codebase already follows; match them rather than introducing
new ones.

**Naming.** Methods are camelCase verb phrases grouped by prefix: `performLocalX` /
`performRemoteX` (TaskProcessor), `sync*` / `fetch*` (SyncWorker, MailStore), `find*` /
`findAll<T>`. Free helper functions in TaskProcessor.cpp carry a leading underscore
(`_moveMessagesResilient`, `_applyUnread`); MailProcessor.cpp uses an anonymous namespace.
Member fields and private methods are underscore-prefixed (`_data`, `_lastSnapshot`,
`_saveInsertQueries`, `_emit`). Tunables and `localStatus` keys are `#define`s (`LS_*`,
`SHALLOW_SCAN_INTERVAL`); file-scope values are `static`. JSON keys are terse for
engine-internal fields (`_sa`, `_suc`, `hMsgId`, `aid`, `v`, `lmrt`) and descriptive for
client-facing ones (`folders`, `labels`, `participants`). SQL identifiers are camelCase;
indexes are named `<Table><What>Index`.

**Logging.** Top-level steps are unprefixed ("Sync loop complete."); sub-steps use `"- "`
then `"-- "`; failures use `"-X "`. Present tense, one line.

**Database access.** SQL is written inline as uppercase literals. A `SQLite::Statement` is
either a local, or cached in a per-purpose map on `MailStore` (`_saveInsertQueries`,
`_placementQueries`) that `rollbackTransaction` drops wholesale; every use is
`bind` → `executeStep`/`exec` → `reset`, and every store method starts with
`assertCorrectThread()`. Model loads go through `findAll<T>(Query)`; joins, counts and bulk
updates are raw SQL. Writes that must batch deltas use
`MailStoreTransaction transaction{store, "<functionName>"}` in a scoped block;
`unsafeEraseTransactionDeltas()` is reserved for saves that change only engine-internal
fields. Chunk sizes: 900 for `findLargeSet`, 500 for thread refreshes, 200 for UID lists,
100 for removes per transaction, with short sleeps between heavy chunks. DDL lives in
`constants.h` as `Vn_SETUP_QUERIES` vectors (`CREATE TABLE IF NOT EXISTS`) that `migrate()`
loops over; a migration that needs its own transaction or a precondition (V10) gets a
dedicated function called from the same place.

**Models.** A `MailModel` subclass declares `static string TABLE_NAME`, constructors
`(id, accountId, version)`, `(SQLite::Statement &)` and `(json)`, `columnsForQuery()` with a
matching `bindToQuery()` binding `:column`, and `beforeSave` / `afterSave` / `afterRemove`
hooks that call the base first. Getters return `json &` for arrays and objects and values for
scalars; setters are `setX(...)`. Derived thread state comes from a before/after
`MessageSnapshot` diff in `Thread::applyMessageAttributeChanges`. Small helper structs live
in the header of their primary user (`MessageAttributes` in MailStore.hpp,
`UIDRangeSyncResult` in SyncWorker.hpp); a struct with methods and several consumers gets its
own file (`Placement.hpp`).

**Errors.** mailcore `ErrorCode` out-params become `throw SyncException(err, "<call>")`;
logical failures are `SyncException("<key>", message, retryable)`. Constraint races on insert
use `catch (SQLite::Exception & ex) { if (ex.getErrorCode() != 19) throw; }`. Non-fatal
remote failures log (`warn`/`error`) and continue.

**Memory.** mailcore objects are created through their autoreleased factories
(`Array::array()`, `IndexSet::indexSet()`, `String` via `MCSTR`/`AS_MCSTR`), never `new`
without `autorelease()`; every thread entry point and every long function that touches
mailcore objects opens with `AutoreleasePool pool;`. Objects returned by `session.fetch*`
are consumed inside the owning pool. C resources from libetpan/curl are freed on every exit
path.

**Comments.** Comment density is roughly 10–20% of lines. `// Note:` for a one-to-three
line inline remark; a `/* */` block above a function only for a non-obvious contract; longer
inline blocks only for protocol or provider quirks, with the issue, commit or spec cited. See
the Comment Style section for what a comment should and should not say.

## Gmail-Specific Behavior
Gmail accounts sync only Spam, All Mail, and Trash folders, using X-GM-LABELS extension for label handling. Virtual folders are ignored. Every message has exactly one placement, in whichever of those three folders holds it (see Message Identity and Placements).

## Adding New Files
New source files must be manually added to:
- `CMakeLists.txt` (Linux)
- `MailSync.xcodeproj` (macOS)
- `Windows/mailsync.vcxproj` (Windows)
