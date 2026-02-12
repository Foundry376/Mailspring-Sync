# FastMail Folder Sync Issue — Root Cause Analysis

**Forum thread**: https://community.getmailspring.com/t/fastmail-seems-to-be-struggling/14208
**Causal commit**: `4653004` ("Improve stability of messages that exist in two folders [iCloud]")

## Symptoms

- Messages archived or read on FastMail (web/phone) don't sync to Mailspring
- New messages arrive correctly
- The issue is permanent — waiting 24+ hours doesn't resolve it
- Account re-add doesn't resolve it (the issue re-occurs once sync catches up)
- Key log message:
  ```
  Message nEd6... staying in FTtf... (priority 0), ignoring 8sLf... (priority 4)
  ```
  (priority 0 = Inbox, priority 4 = Archive)

## Root Cause

Commit `4653004` added a **folder priority system** to `MailProcessor::updateMessage()` (lines 185-218) designed to fix message "flickering" on **iCloud**, where the same message genuinely exists in multiple IMAP folders simultaneously. The logic assigns priorities (inbox=0 > sent=1 > drafts=2 > all=3 > archive=4 > trash=5 > spam=6) and **blocks** any folder change from a higher-priority folder to a lower-priority one unless the message has been "unlinked" (detected as removed) from the source folder first.

On **standard IMAP servers like FastMail**, moving a message from Inbox to Archive is a DELETE from Inbox + APPEND to Archive. The priority logic incorrectly blocks this because:

1. Archive (priority 4) tries to claim a message currently in Inbox (priority 0)
2. The priority check returns early (line 215: `return;`) — **skipping ALL updates including flag changes**
3. The message remains stuck in Inbox forever

## The Race Condition

The dual-worker architecture makes this much worse. The system has:
- A **foreground worker** (IDLE thread) monitoring the Inbox via IMAP IDLE
- A **background worker** syncing ALL folders periodically via `syncNow()`

### Failure sequence:

1. User archives message M on FastMail (M moves from Inbox to Archive)
2. **IDLE fires** → foreground worker syncs Inbox via CONDSTORE
   - CONDSTORE detects modseq changed
   - *If QRESYNC properly reports VANISHED*: M is unlinked ✓ (subsequent Archive sync can reclaim)
   - *If QRESYNC vanished is NULL or incomplete*: M is **NOT unlinked** ✗
   - **In both cases**: Inbox's stored `HIGHESTMODSEQ` is advanced to `remoteModseq`
3. **Background `syncNow()` runs**:
   - **Inbox**: stored modseq matches remote → `syncFolderChangesViaCondstore` **returns early** (line 890-892) → M's vanish is **never re-checked**
   - **Archive**: CONDSTORE detects M → `updateMessage()` called
   - Priority check: M in Inbox (0) vs Archive (4) → **BLOCKED** (line 215: `return`)
4. **M is permanently stuck in Inbox** — the stored modseq has been advanced past the vanish event, so no future sync cycle will ever detect the removal

### Why QRESYNC may fail to report vanished messages:
- The foreground worker checks only `IMAPCapabilityCondstore` (not QRESYNC) to decide whether to use `syncFolderChangesViaCondstore` (line 221)
- If the session's `mQResyncEnabled` flag is false (reconnect without re-login, session state loss), `mailimap_uid_fetch_changedsince` is used instead of `mailimap_uid_fetch_qresync`, producing a NULL vanished set
- libetpan parsing failures could drop the VANISHED response
- The server might not send VANISHED for certain operations

## Why This Didn't Happen Before

Before commit `4653004`, `updateMessage()` had no priority check. When Archive was synced and the message was found there, it would simply update the message's folder — "latest folder wins." This naturally handled folder moves even without reliable vanish detection.

The priority logic introduces a **strict ordering dependency**: the source folder MUST process the removal BEFORE the destination folder processes the addition. Any failure in this ordering (race condition, QRESYNC failure, timing issue) causes permanent message loss from the user's perspective.

## Additional Issue: Flag Updates Blocked

The `return` on line 215 exits `updateMessage()` entirely, skipping not just the folder change but also **flag updates** (read/unread, starred, etc.). Even if the folder change should be blocked, flag changes should still be applied. This explains the user's report that marking messages as read externally also fails to sync.

## Scope of Impact

This affects ALL non-iCloud, non-Gmail IMAP servers where:
- Messages move between folders (not copied)
- The IDLE worker processes folder changes before the background worker
- The destination folder has lower priority than the source folder

FastMail, Yahoo, Outlook.com (IMAP), ProtonMail Bridge, and other standard IMAP servers are all potentially affected.

## Recommended Fixes

### Option A: Restrict priority logic to iCloud only (simplest)
```cpp
// Only apply priority logic for iCloud accounts where messages
// genuinely exist in multiple folders simultaneously
bool isICloud = account->IMAPHost().find("imap.mail.me.com") != string::npos;
if (isICloud && folder.id() != currentFolderId && !currentFolderId.empty()) {
    // ... existing priority logic ...
}
```

### Option B: Allow flag updates even when folder change is blocked
```cpp
if (newPriority >= currentPriority) {
    // Block the folder change but still apply flag updates
    logger->info("- Message {} staying in {} (priority {}), ignoring folder {} (priority {})",
                local->id(), currentFolderId, currentPriority, folder.id(), newPriority);
    // Fall through to update flags, just don't update the folder
    // ... apply flag changes without folder change ...
    return;
}
```

### Option C: Add staleness check (defense in depth)
If a message has been "stuck" in a folder for multiple sync cycles without being seen in that folder's remote results, allow it to be reclaimed by another folder.

### Recommended approach
**Option A** is the safest immediate fix. The priority logic was designed specifically for iCloud's non-standard behavior and should only apply there. Option B is a good follow-up improvement regardless.

## Files Involved

- `MailSync/MailProcessor.cpp` — lines 185-218 (the priority check in `updateMessage()`)
- `MailSync/MailUtils.cpp` — `priorityForFolderRole()` (priority map)
- `MailSync/SyncWorker.cpp` — `idleCycleIteration()` lines 221-234 (IDLE CONDSTORE sync), `syncNow()` lines 412-415 (background CONDSTORE sync)
