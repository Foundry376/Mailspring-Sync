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
- **Background thread** (`SyncWorker`): Iterates folders, performs incremental sync using CONDSTORE/QRESYNC, and runs the end-of-pass tombstone sweep
- **Foreground thread** (`SyncWorker`): IDLEs on primary folder, handles body fetches and task execution
- **CalContacts thread** (`DAVWorker`, `GoogleContactsWorker`): Calendar/contact sync via CardDAV/CalDAV
- **Metadata threads** (`MetadataWorker`, `MetadataExpirationWorker`): Syncs plugin metadata to/from id.getmailspring.com

### Key Components
- `MailStore`: SQLite database wrapper with template-based queries. Uses "fat" rows with a `data` JSON column plus indexed columns for queryable fields. See Reactive Data Flow above.
- `TaskProcessor`: Handles local (immediate) and remote (network) task execution for operations like sending mail, modifying flags, etc.
- `MailProcessor`: Parses IMAP messages, creates stable IDs from headers, upserts placements on ingest, tombstones vanished copies and sweeps expired tombstones (see Message Identity and Placements)

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
syncedAt, unlinkedAt, pendingFolderId)`, unique on `(accountId, folderId, remoteUID)` for
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
- **The snapshot is maintained incrementally by the helpers that write the rows, never
  rebuilt from a query.** Every location change goes through a `MailStore` placement helper
  (`upsertPlacement`, `setPlacementUnread/Starred/Labels`, `beginPlacementMove` /
  `commitPlacementMove`, `removePlacement`, `clearTombstones`,
  `refreshMessageFromPlacements`; bulk SQL-only `tombstonePlacements`, `resetPlacementUIDs`,
  `deleteExpiredTombstones`, `deletePlacementsForMessage/Folder`). The bulk helpers return
  the affected message ids so the caller can refresh and save exactly those messages. This
  invariant is disciplinary, not structural: do not write `MessageFolder` or `folders`
  anywhere else. The test harness reconciles the snapshot against the table after every
  scenario.
- **A move never deletes the `Message`.** When a folder scan (range diff, QRESYNC VANISHED,
  untagged EXPUNGE) finds a copy gone, the placement is *tombstoned* (`unlinkedAt = now`),
  the folder key is dropped from the snapshot and the message saved, so the client sees the
  copy leave that folder within seconds. Any later upsert of a live copy clears the
  message's tombstones. At the end of every background `syncNow` pass,
  `MailProcessor::sweepExpiredTombstones(passStartedAt)` deletes tombstones older than the
  pass start and removes only messages left with zero placements, through `store->remove`
  so `Message::afterRemove` fixes the thread, body and metadata. A plain-IMAP MOVE seen
  source-first therefore streams `persist {folders: {}}` then `persist {folders: {dest}}`
  on the same id — never `unpersist` — and metadata survives. Both workers share this
  grace rule because it is keyed on the pass timestamp, not on a per-worker phase.
- **The `syncedAt` guard is message-level.** `updateMessage` ignores server data older than
  `Message._sa` (set to now + 24h by a task's local phase and reset by its remote phase).
  This is what stops a scan of the *source* folder from resurrecting a copy the user just
  moved away; after the local phase the source placement is the thing in flight, so a
  per-placement timestamp would have nowhere to live.
- **Optimistic moves are pending placements.** A task's local phase sets
  `pendingFolderId` on the placements it decided to move; the snapshot reports them under
  the pending folder, so the client updates immediately. The remote phase MOVEs (or
  COPY + `\Deleted` + EXPUNGE without MOVE) per source folder and rewrites each row in
  place with the new UID. If the destination already holds a live copy, the source copy is
  removed instead of moved. `ChangeFolderTask` carries `sourceFolderIds[]` from the
  client (destination role trash/spam → every placement; otherwise the listed folders, or
  every non-sent/drafts placement when empty), and the engine writes `undoPlacements`
  into the task during its local phase; an undo task carries `restorePlacements`, which
  overrides `folder`/`sourceFolderIds`.
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
  still at UID 0 when the rebuild completes is tombstoned — except drafts.

The design, rationale and migration are in
`../docs/message-placements-plan.md` (client repo). Schema version 10 introduced
`MessageFolder` and rebuilt `Message` without `remoteFolderId`/`remoteUID`/
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

## Gmail-Specific Behavior
Gmail accounts sync only Spam, All Mail, and Trash folders, using X-GM-LABELS extension for label handling. Virtual folders are ignored. Every message has exactly one placement, in whichever of those three folders holds it (see Message Identity and Placements).

## Adding New Files
New source files must be manually added to:
- `CMakeLists.txt` (Linux)
- `MailSync.xcodeproj` (macOS)
- `Windows/mailsync.vcxproj` (Windows)
