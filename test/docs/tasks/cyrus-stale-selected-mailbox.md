# Task: a session whose selected mailbox was replaced on Cyrus never re-SELECTs

Status: open, THEORETICAL on Fastmail, low severity. Found 2026-09-24 bringing up the
`cyrus` server kind (`uidvalidity-change` on cyrus, before the adapter dropped connections
after rebuilding a mailbox), at `050cb39`.

## Symptom

The engine stops syncing a folder for good: every pass and every IDLE cycle fails on the
same command with `ErrorFetch`, sleeps 120 s, and fails again, until the process restarts.
From the engine log (INBOX rebuilt under both connections by `reconstruct`):

```
[foreground] recv * VANISHED 1:80
[foreground] recv * STATUS INBOX (MESSAGES 80 RECENT 0 UIDNEXT 81 UIDVALIDITY 1790278000 ... HIGHESTMODSEQ 161)
[foreground] sent 8 UID FETCH 1:* (UID FLAGS ENVELOPE ...) (CHANGEDSINCE 81 VANISHED)
[foreground] recv 8 NO Mailbox does not exist (0.000 sec)
[background] [warning] UIDInvalidity! Resetting placement UIDs in INBOX, rebuilding index. ...
[background] sent 150 UID FETCH 1:* (UID FLAGS)
[background] recv 150 NO Mailbox does not exist (0.000 sec)
*** {"debuginfo":"syncFolderChangesViaCondstore - syncMessagesByUID","key":"ErrorFetch",...}
*** {"debuginfo":"syncFolderUIDRange - fetchMessagesByUID","key":"ErrorFetch",...}
--sleeping
... 120 s later, the same two NOs, on the same connections, with no SELECT in between.
```

## What Cyrus does

Cyrus 3.6 keeps a session bound to the mailbox instance it SELECTed. When another session
deletes and recreates that mailbox, renames it away, or an admin rebuilds its index, the
bound session is sent `VANISHED` for everything it had, and every later command that needs
the selection answers `NO Mailbox does not exist` (no response code) until the client
SELECTs again. STATUS on the same name works and shows the new UIDVALIDITY. Reproduced over
plain imaplib against the harness image (`cyrus:default-ns`):

```
A: SELECT INBOX.Work                      -> OK
B: DELETE INBOX.Work; CREATE INBOX.Work   -> OK, OK
A: NOOP                                   -> * VANISHED 1:3, OK
A: UID FETCH 1:* (UID FLAGS)              -> NO Mailbox does not exist
A: STATUS INBOX.Work (UIDVALIDITY)        -> OK (new UIDVALIDITY)
A: SELECT INBOX.Work; UID FETCH ...       -> OK
B: RENAME INBOX.Work INBOX.Work2; C (had it selected): UID FETCH -> NO Mailbox does not exist
```

Dovecot instead disconnects the session (`doveadm mailbox update --uid-validity` kicks it),
so the engine reconnects and SELECTs afresh; that is why this never showed on Dovecot.

## Root cause

mailcore's `IMAPSession::selectIfNeeded` (Vendor/mailcore2/src/core/imap/MCIMAPSession.cpp:1211)
skips the SELECT whenever `mCurrentFolder` already names the folder, and a tagged NO on a
FETCH (`ErrorFetch`, e.g. MCIMAPSession.cpp:2882) neither clears `mCurrentFolder` nor sets
`mShouldDisconnect`. The engine throws on it (SyncWorker.cpp:1199
`syncFolderUIDRange - fetchMessagesByUID`, SyncWorker.cpp:1374
`syncFolderChangesViaCondstore - syncMessagesByUID`), the worker sleeps, and the next attempt
sends the same selection-less command on the same connection.

## Why it is theoretical on Fastmail

It needs the folder a connection has selected to be replaced *and* the next command on that
connection to target the same name without a SELECT in between. The foreground connection
only ever idles on INBOX and the background pass starts with INBOX, and a Fastmail user
cannot delete or rename INBOX; for any other folder the background's next SELECT of a
different folder clears the state within one pass. What remains is INBOX being rebuilt or
restored server-side (a Cyrus `reconstruct` by the provider) while Mailspring is connected.
Webmail delete-then-recreate of the folder the background last touched costs one failed pass
(120 s), not a stuck account.

## Fix options

- In mailcore, treat a tagged NO to a command that requires the selection as "selection
  lost": clear `mCurrentFolder` / drop to `STATE_LOGGEDIN` so the next call SELECTs.
- Or in the engine, on `ErrorFetch` from a folder operation, force a re-SELECT (or
  `session.disconnect()`) before rethrowing, so the retry starts clean.

## Definition of done

A harness scenario that replaces a selected mailbox without dropping connections (a
`server.rebuild_mailbox` op on cyrus that skips the adapter's `drop_connections`, or a
delete-and-recreate of INBOX's index) converges within one pass on `cyrus:fastmail`. Delete
this document when it lands.
