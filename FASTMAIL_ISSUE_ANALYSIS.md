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

## Root Cause Summary

Three interacting bugs combine to make folder moves permanently fail on standard IMAP servers:

1. **Priority logic** (commit `4653004`) blocks folder moves from higher to lower priority, designed for iCloud but applied to all servers
2. **VANISHED detection is systematically unreliable** due to IDLE notification consumption and plain SELECT (not QRESYNC SELECT)
3. **Both workers unconditionally advance HIGHESTMODSEQ**, permanently skipping any missed unlink events

## Root Cause 1: Priority Logic (the primary bug)

Commit `4653004` added a **folder priority system** to `MailProcessor::updateMessage()` (lines 185-218) designed to fix message "flickering" on **iCloud**, where the same message genuinely exists in multiple IMAP folders simultaneously. The logic assigns priorities (inbox=0 > sent=1 > drafts=2 > all=3 > archive=4 > trash=5 > spam=6) and **blocks** any folder change from a higher-priority folder to a lower-priority one unless the message has been "unlinked" (detected as removed) from the source folder first.

On **standard IMAP servers like FastMail**, moving a message from Inbox to Archive is a DELETE from Inbox + APPEND to Archive. The priority logic incorrectly blocks this because:

1. Archive (priority 4) tries to claim a message currently in Inbox (priority 0)
2. The priority check returns early (line 215: `return;`) — **skipping ALL updates including flag changes**
3. The message remains stuck in Inbox forever

### Why This Didn't Happen Before

Before commit `4653004`, `updateMessage()` had no priority check. When Archive was synced and the message was found there, it would simply update the message's folder — "latest folder wins." This naturally handled folder moves even without reliable vanish detection.

The priority logic introduces a **strict ordering dependency**: the source folder MUST process the removal BEFORE the destination folder processes the addition. Any failure in this ordering (race condition, QRESYNC failure, timing issue) causes permanent message loss from the user's perspective.

### Additional Issue: Flag Updates Blocked

The `return` on line 215 exits `updateMessage()` entirely, skipping not just the folder change but also **flag updates** (read/unread, starred, etc.). Even if the folder change should be blocked, flag changes should still be applied. This explains the user's report that marking messages as read externally also fails to sync.

## Root Cause 2: VANISHED Detection is Systematically Unreliable

The user's own testing revealed the **smoking gun** — both the foreground and background workers report "0 changed, 0 vanished" for Inbox even when messages have been removed:

```
[foreground] syncFolderChangesViaCondstore - INBOX: modseq 241 to 246, uidnext 18 to 18
[background] syncFolderChangesViaCondstore - INBOX: modseq 241 to 246, uidnext 18 to 18
[foreground] Changes since HMODSEQ 241: 0 changed, 0 vanished
[background] Changes since HMODSEQ 241: 0 changed, 0 vanished
[background] syncFolderChangesViaCondstore - Archive: modseq 238 to 246, uidnext 15 to 16
[background] Changes since HMODSEQ 238: 1 changed, 0 vanished
[background] Message E8Gq... staying in 4CGB... (priority 0), ignoring fEbP... (priority 4)
```

The modseq changed (241→246) but NEITHER worker detected vanished messages. This is not a rare race — it's a **systematic failure** with multiple contributing causes:

### 2a. Foreground worker: IDLE consumes notifications

The foreground worker IDLEs on Inbox (`MCIMAPSession.cpp` line 3696: `mailimap_idle()`). During IDLE, the server sends unsolicited EXPUNGE or VANISHED responses when messages are removed. These are received by libetpan during the IDLE wait (`mailstream_wait_idle()`, line 3714).

When IDLE exits (`mailimap_idle_done()`, line 3739), these notifications have been received and parsed by libetpan. **But the Mailspring code never extracts or processes them** — it only uses the IDLE exit as a signal to call `syncFolderChangesViaCondstore`.

The subsequent `UID FETCH 1:* (FLAGS) (CHANGEDSINCE 241 VANISHED)` may then return "0 vanished" because the server considers this connection already informed about those expunges.

### 2b. Background worker: Plain SELECT doesn't capture VANISHED

The background worker uses `selectIfNeeded()` (`MCIMAPSession.cpp` line 2529) before FETCH. This calls `mailimap_select()` (line 1194) — a **plain SELECT** command.

RFC 7162 defines `SELECT ... (QRESYNC (uidvalidity modseq [known-uids]))` which causes the server to send VANISHED (EARLIER) in the SELECT response. But Mailspring **never uses `mailimap_select_qresync()`** — the function exists in libetpan (`qresync.c` line 238) but is never called from mailcore2.

With QRESYNC enabled (via `ENABLE QRESYNC`), a plain SELECT can trigger the server to send pending notifications for this connection. If the background worker had Inbox selected in a previous sync cycle, the server may deliver pending EXPUNGE/VANISHED notifications during the re-SELECT. These are parsed by libetpan but **not captured by the code**, since `select()` doesn't check for VANISHED in the response.

The subsequent FETCH CHANGEDSINCE then returns "0 vanished" because the server already notified this connection during SELECT.

### 2c. CONDSTORE fallback doesn't report vanished at all

The developer explicitly acknowledged this limitation at `SyncWorker.cpp` lines 218-220:
```cpp
// Note: If we have CONDSTORE but don't have QRESYNC, this if/else may result
// in us not seeing "vanished" messages until the next shallow sync iteration.
// Right now I think that's fine.
```

When CONDSTORE is available but QRESYNC is not (or `mQResyncEnabled` is false), the code uses `mailimap_uid_fetch_changedsince()` (`condstore.c` line 169) which calls `mailimap_uid_fetch_qresync_vanished()` with `vanished=0` — explicitly NOT requesting VANISHED data. The result: `vanished` is always NULL.

The developer assumed shallow scans would catch up. But with the priority logic from commit `4653004`, messages in higher-priority folders can never be reclaimed by lower-priority folders even when a shallow scan finds them there.

### 2d. enableFeature return value is not checked

`enableFeatures()` (`MCIMAPSession.cpp` line 4598) calls `enableFeature(MCSTR("QRESYNC"))` but **ignores the return value**. If the server rejects `ENABLE QRESYNC` (returns NO or BAD), `mQResyncEnabled` remains true (set earlier by `applyCapabilities()`), but the server doesn't actually have QRESYNC enabled. The subsequent FETCH with VANISHED modifier may be silently ignored.

## Root Cause 3: The Dual-Worker Race Condition

The system has two SyncWorker instances with **separate IMAPSessions** sharing one **SQLite database** (`main.cpp` lines 60-61: `bgWorker` and `fgWorker`).

### Failure sequence (matching user's logs):

1. User flags+archives message M on FastMail (M moves from Inbox to Archive)
2. **Both workers read stored modseq 241 from SQLite simultaneously**
3. Both call `syncFolderChangesViaCondstore` for Inbox with modseq 241→246
4. Both get "0 changed, 0 vanished" (see Root Cause 2)
5. **Both advance HIGHESTMODSEQ to 246** (`SyncWorker.cpp` lines 950-951) — unconditionally, even though vanished was NULL
6. Background worker syncs Archive → finds message M (1 changed)
7. `updateMessage()` called → priority check: Inbox(0) vs Archive(4) → **BLOCKED** (line 215: `return`)
8. **Message M is permanently stuck in Inbox** — modseq has been advanced past the vanish event, so no future sync will detect the removal

### Why single archive works but multiple actions don't:

The user observed: "single archive → works, flag+archive → doesn't"

For a single archive with favorable timing:
- Only one event (EXPUNGE) changes the modseq
- One worker may successfully capture the VANISHED
- The message gets unlinked before the other folder claims it

For multiple simultaneous actions (flag + archive):
- Multiple events change the modseq
- IDLE receives notifications for multiple changes
- Both workers race to process the same modseq range
- VANISHED is consumed by notifications during IDLE/SELECT but not processed
- Neither worker unlinks the message

## Scope of Impact

This affects ALL non-iCloud, non-Gmail IMAP servers where:
- Messages move between folders (not copied)
- The destination folder has lower priority than the source folder
- Priority logic was designed for iCloud but applied universally

FastMail, Yahoo, Outlook.com (IMAP), ProtonMail Bridge, and other standard IMAP servers are all potentially affected.

## Recommended Fixes

### Fix 1 (Critical): Restrict priority logic to iCloud only

```cpp
// In MailProcessor::updateMessage(), lines 185-218
// Only apply priority logic for iCloud accounts where messages
// genuinely exist in multiple folders simultaneously
bool isICloud = account->IMAPHost().find("imap.mail.me.com") != string::npos;
if (isICloud && folder.id() != currentFolderId && !currentFolderId.empty()) {
    // ... existing priority logic ...
}
// For all other servers, allow "latest folder wins" behavior
```

This is the **safest immediate fix**. The priority logic was designed specifically for iCloud's non-standard behavior (messages exist in multiple folders simultaneously) and should only apply there. For standard IMAP servers, "latest folder wins" correctly handles folder moves.

### Fix 2 (Important): Allow flag updates even when folder change is blocked

Even for iCloud, the `return` at line 215 should NOT skip flag updates:

```cpp
if (newPriority >= currentPriority) {
    logger->info("- Message {} staying in {} (priority {}), ignoring folder {} (priority {})",
                local->id(), currentFolderId, currentPriority, folder.id(), newPriority);
    // Don't return — fall through to apply flag updates below
    // Just skip the folder ID/path update
    skipFolderUpdate = true;
}
// ... rest of updateMessage applies flags regardless of skipFolderUpdate ...
```

### Fix 3 (Defense in depth): Add staleness check

If a message has been in a folder for multiple sync cycles without being seen in that folder's remote FETCH results, it should be allowed to move:

```cpp
// Track "last seen in folder" timestamp per message
// If message hasn't been confirmed in current folder for N cycles,
// allow reclaim by another folder
```

### Fix 4 (Long-term): Use QRESYNC SELECT and process IDLE notifications

- Call `mailimap_select_qresync()` instead of `mailimap_select()` to capture VANISHED in SELECT responses
- After IDLE exits, process EXPUNGE/VANISHED notifications from the IDLE session before calling FETCH CHANGEDSINCE
- Check `enableFeature()` return value and fall back gracefully if ENABLE QRESYNC fails

### Recommended approach

**Fix 1** is the highest-priority immediate fix — it eliminates the root cause for all non-iCloud servers. **Fix 2** should be applied alongside it to prevent flag sync failures on iCloud. Fixes 3 and 4 are defense-in-depth improvements.

## Files Involved

| File | Lines | Issue |
|------|-------|-------|
| `MailSync/MailProcessor.cpp` | 185-218 | Priority check in `updateMessage()` — the primary bug |
| `MailSync/MailUtils.cpp` | 386-417 | `priorityForFolderRole()` — priority map |
| `MailSync/SyncWorker.cpp` | 218-222 | Foreground worker: CONDSTORE check, acknowledged VANISHED gap |
| `MailSync/SyncWorker.cpp` | 412-415 | Background worker: CONDSTORE+QRESYNC gate |
| `MailSync/SyncWorker.cpp` | 950-951 | Unconditional HIGHESTMODSEQ advance |
| `MailSync/SyncWorker.cpp` | 247-252 | IDLE: notifications not processed after exit |
| `Vendor/mailcore2/src/core/imap/MCIMAPSession.cpp` | 1107-1125 | `selectIfNeeded()` uses plain SELECT |
| `Vendor/mailcore2/src/core/imap/MCIMAPSession.cpp` | 1194 | `select()` calls `mailimap_select()` not `mailimap_select_qresync()` |
| `Vendor/mailcore2/src/core/imap/MCIMAPSession.cpp` | 2701-2710 | QRESYNC vs CONDSTORE FETCH decision |
| `Vendor/mailcore2/src/core/imap/MCIMAPSession.cpp` | 4597-4598 | `enableFeatures()` ignores return value |
| `Vendor/libetpan/src/low-level/imap/qresync.c` | 238 | `mailimap_select_qresync()` — exists but never called |
| `Vendor/libetpan/src/low-level/imap/qresync.c` | 436-443 | `mailimap_uid_fetch_qresync()` — sends VANISHED modifier |
| `Vendor/libetpan/src/low-level/imap/condstore.c` | 169-175 | `mailimap_uid_fetch_changedsince()` — does NOT request VANISHED |
| `MailSync/main.cpp` | 60-61 | Two separate SyncWorker instances sharing SQLite |
