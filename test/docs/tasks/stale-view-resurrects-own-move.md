# Task: on servers without QRESYNC, a stale FETCH re-adds the copy the engine just moved

Status: open. Found 2026-09-20 by the integration harness on the placements branch
(`f5c41b8`). Reproduced on the fake and on real Dovecot (`dovecot:plain`). Same root cause as
`stale-view-delays-deep-scan-deletions.md` (fix options there); a fix that syncs the background
connection's view before it fetches closes both.

## Symptom

The engine moves a copy out of folder F on the foreground connection (here: the undo of an
archive, `UID MOVE 4 INBOX` with Archive selected, via `restorePlacements`), confirms the
placement move locally (Archive gone, INBOX present - correct), and then the next background
pass re-adds an Archive placement for the same UID:

```
[background] syncFolderUIDRange for Archive, UIDs: 4 - 5, Heavy: true
[background] - Archive: remote=1, local=0
[background] - Message ... has a copy in Archive (UID 4)      <- ghost
...next pass...
[background] - Archive: remote=0, local=1
[background] Tombstoning 1 UIDs no longer present in Archive.  <- gone again
```

For one pass the client shows the message in both INBOX and Archive; `stable` reports the
placement appearing and vanishing. On QRESYNC servers the VANISHED on the background
connection prevents it.

## Failing test

- `test/scenarios/undo-move-restores-placements.yaml`, expectations `counts`,
  `db_matches_server`, `stable`, marked `xfail` on `fake:plain` and `dovecot:plain`. Remove
  the markers once fixed; `fake:dovecot` / `dovecot:qresync` pass today.

```bash
python3 test/run.py test/scenarios/undo-move-restores-placements.yaml --server dovecot:plain --keep
grep -E "has a copy in Archive|Tombstoning .* Archive" test/runs/undo-move-restores-placements-dovecot-plain/config/mailsync-*.log*
```

## Root cause

Two things combine:

1. **The background connection keeps Archive selected** from the pass that ingested the
   moved-in copy (`SELECT Archive` at the first move; Trash and Junk after it are empty and
   are only STATUSed). Dovecot answers a `FETCH` on that connection from its own view until
   the connection is told about the other session's expunge, and the EXPUNGE line arrives
   *after* the FETCH data (README "Things learned from Dovecot", `probe_stale_fetch`).
2. **The plain (non-CONDSTORE) new-mail path re-runs every pass.** `syncNow` fetches
   `RangeMake(localUidnext, remoteUidnext - localUidnext)` whenever `remoteUidnext >
   localUidnext`, but only the shallow/deep scan branches write `localStatus[LS_UIDNEXT]`
   (SyncWorker.cpp `if (newMessages)` block vs `timeForShallowScan` / `timeForDeepScan`).
   Between scan intervals the same `UID FETCH 4:5` is issued on every pass, and the first
   one after the move is answered from the stale view. `syncFolderUIDRange` upserts the
   listed UID as a live placement.

`docs/message-placements-plan.md` 2.9 (non-QRESYNC servers) describes the fix: when a scan
adds a placement for a message that already has one elsewhere on a non-QRESYNC session,
queue `(folder, uid)` and issue one `UID FETCH ... (UID)` at the end of the pass, tombstoning
what the server does not return ("Phase 5"). A cheaper partial fix: advance `LS_UIDNEXT`
after the new-mail fetch so the range is not re-fetched, and/or `UNSELECT`/re-`SELECT` before
a scan whose folder the connection already had open.
