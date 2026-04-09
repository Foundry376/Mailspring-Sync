# EML Export Implementation Guide

This document describes the architecture for EML (.eml) export in Mailspring, covering both the mailsync (C++) and client (Electron/TypeScript) layers. The mailsync side is implemented; the client side is described here as a specification for the Mailspring (client) repo.

## Background

EML files are raw RFC2822 message data — the exact bytes as stored on the IMAP server. They cannot be reliably reconstructed from parsed components (headers, HTML body, attachments stored separately). Therefore, all EML export flows through mailsync, which has the IMAP session needed to fetch raw message data via MailCore2's `fetchMessageByUID()`.

Community request history: [community#325](https://community.getmailspring.com/t/export-copy-backup-emails/325), GitHub issues #264, #1012, #1368, and draft PR #2422.

## Architecture Overview

There are two task types in mailsync that power all EML export features:

| Task | Purpose | Implemented In |
|------|---------|----------------|
| `GetMessageRFC2822Task` | Fetch a single message's RFC2822 data, write to a file path | `TaskProcessor.cpp` (pre-existing) |
| `GetManyRFC2822Task` | Export all messages in a folder to a directory as .eml files | `TaskProcessor.cpp` (new) |

These tasks are used by four client-side features:

| Feature | UI Trigger | Task Used | Notes |
|---------|-----------|-----------|-------|
| Save single email as .eml | Right-click message → "Download Email" | `GetMessageRFC2822Task` | Client shows save dialog, passes filepath |
| Drag-and-drop to desktop | Drag message from list to OS file manager | `GetMessageRFC2822Task` | Platform-specific drag promise, resolved with task result |
| Forward as attachment | Reply menu → "Forward as Attachment" | `GetMessageRFC2822Task` | Result .eml file attached to compose window |
| Export folder | Right-click folder → "Export Folder" | `GetManyRFC2822Task` | Client shows directory picker, shows progress bar |

---

## Mailsync Tasks (C++ — Mailspring-Sync repo)

### GetMessageRFC2822Task (pre-existing)

**Input JSON:**
```json
{
  "type": "queue-task",
  "task": {
    "__cls": "GetMessageRFC2822Task",
    "aid": "<account-id>",
    "messageId": "<message-id>",
    "filepath": "/path/to/output.eml"
  }
}
```

**Behavior:**
1. Looks up the Message by ID in SQLite
2. Calls `session->fetchMessageByUID()` using the message's remote folder path and UID
3. Writes raw RFC2822 bytes to `filepath` via `Data::writeToFile()`
4. Task status becomes `"complete"` (handled by the `performRemote` wrapper)

**Error handling:** Throws `SyncException` on IMAP errors or null data — task status becomes `"complete"` with an error field set.

**Location:** `TaskProcessor.cpp:1826-1853`

---

### GetManyRFC2822Task (new)

**Input JSON:**
```json
{
  "type": "queue-task",
  "task": {
    "__cls": "GetManyRFC2822Task",
    "aid": "<account-id>",
    "folderId": "<folder-id>",
    "folderPath": "INBOX",
    "outputDir": "/home/user/Desktop/Inbox Export"
  }
}
```

**Required fields:**
- `folderId` — The Mailspring folder ID (used to query messages from SQLite)
- `folderPath` — The IMAP folder path string (used for `fetchMessageByUID`)
- `outputDir` — Absolute path to the output directory (must exist; client creates it before queuing)

**Behavior:**

1. Queries all messages in the folder: `findAll<Message>(Query().equal("accountId", aid).equal("remoteFolderId", folderId))`
2. Checks for resume state in `task.data.progress.exported` (non-zero means we're resuming after a previous interruption)
3. Processes messages in chunks of 50
4. For each message:
   - Generates a sanitized filename: `{index} - {subject} - {date}.eml` (see Filename Sanitization below)
   - Fetches full RFC2822 via `session->fetchMessageByUID()`
   - Writes to `outputDir/filename`
   - On per-message IMAP errors: logs the error, increments `failed` count, continues to next message (does NOT abort the export)
5. After each chunk of 50:
   - Saves progress to task data (emits delta to client for progress bar)
   - Re-reads the task from DB to check `should_cancel` flag
   - Sleeps 1 second to let sync and other tasks breathe
6. On completion, writes final result to `task.data.result`

**Progress reporting (task.data.progress):**
```json
{
  "total": 1500,
  "exported": 350,
  "failed": 2,
  "errors": [
    {"messageId": "abc", "subject": "...", "error": "ErrorFetch"},
    {"messageId": "def", "subject": "...", "error": "null data"}
  ]
}
```

Progress is written to `task.data["progress"]` and saved via `store->save(task)` after each chunk of 50. This triggers a `persist` delta for the Task model, which the client receives and can use to update a progress bar.

**Final result (task.data.result):**
```json
{
  "total": 1500,
  "exported": 1498,
  "failed": 2,
  "outputDir": "/home/user/Desktop/Inbox Export",
  "errors": [...]
}
```

**Cancellation:**

Before each chunk, the task re-reads itself from the database to check the `should_cancel` flag. The client can cancel by sending:
```json
{"type": "cancel-task", "taskId": "<task-id>"}
```
This sets `should_cancel = true` on the task in the DB. On cancellation, progress is saved with `"cancelled": true` and the task returns (status will be set to `"complete"` by the `performRemote` wrapper — the client should check for the `cancelled` flag in progress data).

**Resume on restart:**

If mailsync crashes or is restarted mid-export, the task will still be in `"remote"` status. On the next `idleCycleIteration`, it will be picked up again. The implementation reads `task.data.progress.exported` to determine the resume index and skips already-exported messages. Files already written to disk are not re-fetched.

**Location:** `TaskProcessor.cpp:1915-2034`

---

### Filename Sanitization

The static helper `TaskProcessor::sanitizeEmlFilename(subject, date, index)` generates safe filenames:

**Format:** `{index} - {subject} - {date}.eml`
- Example: `00042 - Meeting notes from Thursday - 2026-04-09.eml`

**Rules:**
1. Subject truncated to 80 characters
2. Filesystem-illegal characters replaced with `_`: `/ ? < > \ : * | "` and control characters (0x00-0x1f, 0x7f)
3. Trailing dots and spaces stripped (Windows requirement)
4. Empty subjects become `"untitled"`
5. Date formatted as `YYYY-MM-DD` in UTC
6. Index is zero-padded to 5 digits for sort order
7. **Windows only:** Total filename clamped to 200 characters to stay within MAX_PATH limits

The 5-digit index prefix ensures:
- Deterministic, collision-free filenames even with duplicate subjects
- Files sort naturally in file managers by export order (which matches message UID order in the folder)

---

## Client Implementation Guide (Mailspring repo)

The following sections describe what needs to be built in the Mailspring client (Electron/TypeScript). This extends the work in draft PR #2422.

### 1. Single Email Download (right-click → "Download Email")

**Already partially implemented in PR #2422.** The PR adds a "Download Email" item to the message reply dropdown in `message-controls.tsx`.

**Flow:**
1. User right-clicks a message → "Download Email"
2. Client shows a Save dialog (`dialog.showSaveDialog`) with `.eml` filter
3. Client generates a filename using the same pattern as `sanitizeEmlFilename` (port the logic to TypeScript, or just use a simpler `{subject} - {date}.eml` format since collisions don't matter for single files)
4. Client queues `GetMessageRFC2822Task` via stdin:
   ```json
   {"type": "queue-task", "task": {"__cls": "GetMessageRFC2822Task", "aid": "...", "messageId": "...", "filepath": "/chosen/path.eml"}}
   ```
5. Task completes → file exists at the chosen path. Client can show a success notification.

**Filename sanitization in TypeScript** (port from PR #2422's `RegExpUtils` additions):
```typescript
function sanitizeFilename(name: string): string {
  return name
    .replace(/[/?<>\\:*|"]/g, '_')
    .replace(/[\x00-\x1f\x80-\x9f]/g, '_')
    .replace(/[. ]+$/, '')
    .substring(0, 80) || 'untitled';
}
```

### 2. Drag-and-Drop to Desktop

**New feature.** This allows users to drag a message from the message list onto their desktop or file manager to create a .eml file.

**Implementation approach:**
- Register a drag handler on message list items (likely in `ThreadListItem` or `MessageListItem`)
- On drag start, use Electron's `webContents.startDrag()` with a placeholder file
- The challenge: `GetMessageRFC2822Task` is async (requires IMAP fetch), but drag-and-drop expects a file to exist at drag time
- **Recommended approach:** Write the .eml to a temp directory first, then initiate the drag. This means:
  1. On drag initiation, queue `GetMessageRFC2822Task` targeting a temp file
  2. Wait for task completion (watch for task delta with `status: "complete"`)
  3. Call `webContents.startDrag({ file: tempPath, icon: emlIcon })`
- Alternative: Use the platform's file promise APIs (`NSFilePromiseProvider` on macOS) which support async resolution. This is more native but significantly more complex and platform-specific.

**Recommended for v1:** Start with the simpler "export to temp, then drag" approach. The slight delay is acceptable and avoids platform-specific code.

### 3. Forward as Attachment

**New feature.** Allows forwarding an email with the original attached as a `.eml` file (RFC2822 `message/rfc822` MIME type).

**Flow:**
1. User clicks Reply dropdown → "Forward as Attachment"
2. Client queues `GetMessageRFC2822Task` targeting a temp file
3. On task completion, open a new compose window with:
   - Subject: `Fwd: {original subject}`
   - The `.eml` file attached with MIME type `message/rfc822`
4. The compose/send flow handles the attachment like any other file

**Key consideration:** The attachment must be sent with `Content-Type: message/rfc822`, not `application/octet-stream`. Verify that Mailspring's attachment handling supports explicit MIME type overrides.

### 4. Folder Export (right-click folder → "Export Folder")

**Partially implemented in PR #2422** (the folder context menu and directory picker). Needs to be rewired to use `GetManyRFC2822Task` instead of queuing N individual tasks.

**Flow:**
1. User right-clicks a folder → "Export Folder"
2. Client shows a directory picker (`dialog.showOpenDialog` with `properties: ['openDirectory', 'createDirectory']`)
3. Client queues a single `GetManyRFC2822Task`:
   ```json
   {
     "type": "queue-task",
     "task": {
       "__cls": "GetManyRFC2822Task",
       "aid": "<account-id>",
       "folderId": "<folder-id>",
       "folderPath": "<imap-folder-path>",
       "outputDir": "/chosen/directory"
     }
   }
   ```
4. Client watches for Task model deltas to update a progress bar:
   ```typescript
   // In a React component or store listener:
   const progress = task.data.progress;
   const percent = progress.total > 0 ? (progress.exported / progress.total) * 100 : 0;
   // Render: "Exporting... 350 / 1500 (2 failed)"
   ```
5. Client provides a "Cancel" button that sends:
   ```json
   {"type": "cancel-task", "taskId": "<task-id>"}
   ```
6. On completion (task status = "complete"), show a summary notification:
   - Success: "Exported 1498 of 1500 emails to /path (2 failed)"
   - If errors: optionally show which messages failed

**Multi-folder export:** To export an entire account, the client should queue one `GetManyRFC2822Task` per folder, each targeting a subdirectory: `outputDir/FolderName/`. The tasks will execute sequentially (foreground worker processes one at a time). The client can show aggregate progress across all folder tasks.

**Folder path for Gmail:** Gmail uses `[Gmail]/All Mail`, `[Gmail]/Trash`, etc. The `folderPath` field must be the IMAP path, not the display name. Get this from `folder.path()` (the Folder model's `path` field in the client).

### 5. Excluded folders

The client should prevent export on virtual/system folders that don't contain real messages:
- Drafts (contains local drafts, not IMAP messages)
- Starred / Unread (virtual folders, not real IMAP folders)

Check `folder.role` before showing the export option. The PR #2422 code already does this.

---

## Testing

### Manual testing checklist

- [ ] Single export: Right-click a message → Download Email → verify .eml opens in Thunderbird/other client
- [ ] Folder export: Export a small folder (~10 messages) → verify all .eml files are present and valid
- [ ] Large folder: Export a folder with 500+ messages → verify progress updates appear, export completes
- [ ] Cancellation: Start a folder export, cancel mid-way → verify it stops, progress shows `cancelled: true`
- [ ] Resume: Start a folder export, kill mailsync mid-way, restart → verify export resumes from where it left off
- [ ] Error handling: Export a folder where some messages have been deleted server-side → verify export continues past failures
- [ ] Filename sanitization: Export messages with special characters in subject (`/ : * ?` etc.) → verify filenames are valid
- [ ] Windows path length: Export messages with very long subjects on Windows → verify filenames are clamped
- [ ] Gmail: Export `[Gmail]/All Mail` → verify messages export correctly despite Gmail's label-based virtualization

### Verifying .eml validity

A valid .eml file:
- Starts with RFC2822 headers (`From:`, `To:`, `Subject:`, `Date:`, etc.)
- Can be opened by Thunderbird, Apple Mail, Outlook, or any RFC2822-compliant client
- Contains full MIME structure including attachments
- Is byte-identical to what the IMAP server stores (since we fetch with `fetchMessageByUID` and write directly)

---

## File Reference (Mailspring-Sync repo)

| File | What changed |
|------|-------------|
| `MailSync/TaskProcessor.hpp` | Added `performRemoteGetManyRFC2822` and `sanitizeEmlFilename` declarations |
| `MailSync/TaskProcessor.cpp` | Added `GetManyRFC2822Task` routing in `performLocal`/`performRemote`, plus full implementation (~120 lines) and filename sanitizer (~60 lines) |
