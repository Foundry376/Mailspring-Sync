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
```cmd
cd Windows
msbuild.exe mailsync.sln /property:Configuration=Release;Platform=Win32
```

## Running Mailsync

Requires environment variables and command-line arguments:
```bash
CONFIG_DIR_PATH=/path/to/config IDENTITY_SERVER=https://id.getmailspring.com \
  ./mailsync --identity "<identity-json>" --account "<account-json>" --mode sync
```

Modes: `sync`, `test` (auth validation), `reset` (clear account data), `migrate` (run migrations)

For debugging in Xcode/Visual Studio, configure the debugger to pass `--identity`, `--account`, and `--mode` arguments.

## Architecture

### Process Model
Each mailsync process handles a single email account. Mailspring runs one process per connected account. The process communicates via:
- **stdout**: Emits newline-separated JSON for all model changes (reactive data pattern)
- **stdin**: Accepts JSON task objects and commands (queue-task, cancel-task, wake-workers, need-bodies)

### Threading Model
- **Main thread**: Listens on stdin for tasks and commands
- **Background thread** (`SyncWorker`): Iterates folders, performs incremental sync using CONDSTORE/XYZRESYNC
- **Foreground thread** (`SyncWorker`): IDLEs on primary folder, handles body fetches and task execution
- **CalContacts thread** (`DAVWorker`, `GoogleContactsWorker`): Calendar/contact sync via CardDAV/CalDAV
- **Metadata threads** (`MetadataWorker`, `MetadataExpirationWorker`): Syncs plugin metadata to/from id.getmailspring.com

### Key Components
- `MailStore`: SQLite database wrapper with template-based queries. Uses "fat" rows with a `data` JSON column plus indexed columns for queryable fields.
- `TaskProcessor`: Handles local (immediate) and remote (network) task execution for operations like sending mail, modifying flags, etc.
- `DeltaStream`: Broadcasts model changes to stdout for the Electron UI
- `MailProcessor`: Parses IMAP messages, creates stable IDs from headers

### Models (in `MailSync/Models/`)
Account, Message, Thread, Folder, Label, Contact, ContactBook, ContactGroup, Calendar, Event, File, Task, Identity

### Vendor Libraries
Located in `Vendor/` - both libetpan and mailcore2 contain local modifications from upstream.

## Gmail-Specific Behavior
Gmail accounts sync only Spam, All Mail, and Trash folders, using X-GM-LABELS extension for label handling. Virtual folders are ignored.

## Adding New Files
New source files must be manually added to:
- `CMakeLists.txt` (Linux)
- `MailSync.xcodeproj` (macOS)
- `Windows/mailsync.vcxproj` (Windows)
