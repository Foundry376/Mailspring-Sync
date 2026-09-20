# Task: SIGSEGV in the VANISHED accumulator when the IMAP session dies around IDLE

Status: open. Found 2026-09-19 by the integration harness. Reproduces on the pre-placements
engine (`0df7864`) and on the placements branch.

## Symptom

The foreground worker segfaults, taking the whole mailsync process down (the client then
shows the account in an error state and relaunches it). Two triggers, same code:

1. The server closes every connection while the foreground worker is idling (server
   restart, NAT timeout, laptop wake). Stack:

   ```
   *** A segmentation fault (SIGSEGV) occurred during program execution: dereference a pointer that is null or invalid.
   *** MCIMAPSession.cpp:895   mailcore::IMAPSession::connectIfNeeded(mailcore::ErrorCode*)
   *** MCIMAPSession.cpp:913   mailcore::IMAPSession::loginIfNeeded(mailcore::ErrorCode*)
   *** MCIMAPSession.cpp:1209  mailcore::IMAPSession::selectIfNeeded(mailcore::String*, mailcore::ErrorCode*)
   *** MCIMAPSession.cpp:3777  mailcore::IMAPSession::idle(mailcore::String*, unsigned int, mailcore::ErrorCode*)
   *** SyncWorker.cpp:282      SyncWorker::idleCycleIteration()
   *** main.cpp:148            runForegroundSyncWorker()
   ```
   Line 895 is the `collectVanishedFromLastResponse()` call at the top of `connectIfNeeded`.

2. Dovecot changes a mailbox's UIDVALIDITY (`doveadm mailbox update --uid-validity`), which
   makes Dovecot drop the idling session. Stack:

   ```
   *** MCIMAPSession.cpp:4585  mailcore::IMAPSession::takeVanishedMessages(mailcore::String*)
   *** SyncWorker.cpp:250      SyncWorker::idleCycleIteration()
   *** main.cpp:148            runForegroundSyncWorker()
   ```

Both functions were added by PR #141 ("Fix QRESYNC VANISHED handling to prevent message
loss"), which made VANISHED collection run on every command and moved the accumulator into
`IMAPSession` (`mVanishedMessages`, keyed by folder).

## Failing tests

- `test/scenarios/connection-dropped-during-idle.yaml` on `fake:dovecot` and `dovecot:qresync`
  (expectations `running`, `db_matches_server`, `counts` are marked `xfail`).
- `test/scenarios/uidvalidity-change.yaml` on `dovecot:qresync` (per-server `xfail`); the
  fake does not drop the session on a UIDVALIDITY change, so `fake:*` passes there.

Reproduce in ~10 s:

```bash
python3 test/run.py test/scenarios/connection-dropped-during-idle.yaml --server fake:dovecot --keep
python3 test/run.py test/scenarios/uidvalidity-change.yaml --server dovecot:qresync --keep
# stderr.log in test/runs/<name>/ has the stack; config/mailsync-*.log has the IMAP transcript
```

## How it was found

The very first end-to-end run of the new fake server had a bug in its IDLE handler that
made it answer `DONE` with `* BYE internal error` and close the socket. The engine's next
action on that session was the crash above. After the fake was fixed the crash was turned
into a deliberate scenario (`server.drop_connections`), and the UIDVALIDITY variant appeared
when the Dovecot suite was first run against the placements branch.

## Likely root cause

`IMAPSession::unsetup()` (MCIMAPSession.cpp:689) frees `mImap` (`mailimap_free`) and nulls
the member, but `collectVanishedFromLastResponse()` and `takeVanishedMessages()` are reached
from paths where the session state is inconsistent after an unexpected disconnect:

- `collectVanishedFromLastResponse()` guards on `mImap == NULL` and
  `mImap->imap_response_info == NULL`, then walks `imap_response_info->rsp_extension_list`
  with `clist_begin()`. After libetpan reports a stream error mid-IDLE, `imap_response_info`
  can be non-NULL while `rsp_extension_list` is NULL (or already freed by
  `mailimap_response_info_free` during error handling), so `clist_begin` dereferences garbage.
  A `rsp_extension_list == NULL` check, and calling the collector only when the last command
  completed without a stream error, are the first things to try.
- `takeVanishedMessages(folder)` looks up `mVanishedMessages->objectForKey(folder)`; on the
  UIDVALIDITY path `SyncWorker::idleCycleIteration` (SyncWorker.cpp:250) calls it after
  `session.idle()` returned an error, at which point `unsetup()` has run
  `mVanishedMessages->removeAllObjects()` from another code path or the folder String passed
  in was released with the failed session state. Check the object lifetimes across
  `unsetup()` and whether `idleCycleIteration` should skip the VANISHED drain when `idle()`
  reported an error.

The pre-#141 engine did not crash on either trigger (it simply reconnected), so the fix is in
the accumulator's error paths, not in the reconnect logic.

## Definition of done

- Both scenarios pass on `fake:dovecot` and `dovecot:qresync` with the `xfail` markers
  removed (they will show as `XPASS` until removed).
- After the drop, the log shows the foreground worker reconnecting, re-selecting INBOX and
  ingesting the two messages appended afterwards (`counts: {INBOX: 22}`).
- `python3 -m pytest test -k "qresync or vanished"` still passes: the fix must not lose the
  VANISHED-on-non-IDLE-command coverage #141 added (`qresync-vanished-during-body-fetch`).
