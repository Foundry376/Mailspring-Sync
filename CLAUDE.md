# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Mailspring-Sync is the native C++11 sync engine for the Mailspring email client. It handles email, contact, and calendar synchronization via IMAP/SMTP using MailCore2, storing data in SQLite with a JSON-based schema.

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
- **Background thread** (`SyncWorker`): Iterates folders, performs incremental sync using CONDSTORE/XYZRESYNC
- **Foreground thread** (`SyncWorker`): IDLEs on primary folder, handles body fetches and task execution
- **CalContacts thread** (`DAVWorker`, `GoogleContactsWorker`): Calendar/contact sync via CardDAV/CalDAV
- **Metadata threads** (`MetadataWorker`, `MetadataExpirationWorker`): Syncs plugin metadata to/from id.getmailspring.com

### Key Components
- `MailStore`: SQLite database wrapper with template-based queries. Uses "fat" rows with a `data` JSON column plus indexed columns for queryable fields. See Reactive Data Flow above.
- `TaskProcessor`: Handles local (immediate) and remote (network) task execution for operations like sending mail, modifying flags, etc.
- `MailProcessor`: Parses IMAP messages, creates stable IDs from headers

### Models (in `MailSync/Models/`)
Account, Message, Thread, Folder, Label, Contact, ContactBook, ContactGroup, Calendar, Event, File, Task, Identity

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
Gmail accounts sync only Spam, All Mail, and Trash folders, using X-GM-LABELS extension for label handling. Virtual folders are ignored.

## Adding New Files
New source files must be manually added to:
- `CMakeLists.txt` (Linux)
- `MailSync.xcodeproj` (macOS)
- `Windows/mailsync.vcxproj` (Windows)
