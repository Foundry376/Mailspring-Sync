# Task: on servers without QRESYNC, the deep scan sees a remote deletion one pass late

Status: open. Found 2026-09-19 by the integration harness on `0df7864`; still present on the
placements branch. Reproduced on the fake (`fake:plain`) and on real Dovecot (`dovecot:plain`).
Same root cause as `stale-view-resurrects-own-move.md`; a fix that syncs the connection's
view before a background fetch closes both.

## Symptom

Another client expunges messages from INBOX (and Archive) on a server that advertises
neither CONDSTORE nor QRESYNC. The only way the engine can notice is the deep scan, which
compares the UIDs a `UID FETCH 1:* (UID FLAGS)` returns against the local placements. The
first deep scan after the deletion reports the folder unchanged:

```
[background] syncFolderUIDRange for INBOX, UIDs: 1 - *, Heavy: false
[background] - INBOX: remote=200, local=200, folderId=...      <- 11 of these were expunged
```

and the messages stay in the client until the *next* deep scan (10 minutes later by
default), which then tombstones them. Nothing is lost, but a deletion made in webmail is
invisible for up to a scan interval longer than it needs to be, and any placement logic that
trusts "the FETCH listed it" as "it is on the server" is being fed rows that are already gone.

## Failing test

- `test/scenarios/plain-expunge-found-by-deep-scan.yaml` on `fake:plain` and
  `dovecot:plain`: the mid-scenario `assert: db_matches_server` after the first `force_scans`
  is marked `xfail`; the second `force_scans` proves convergence. Remove the `xfail` (and
  the second `force_scans`) once fixed.

```bash
python3 test/run.py test/scenarios/plain-expunge-found-by-deep-scan.yaml --server dovecot:plain --keep
# report.txt shows the XFAIL; grep 'INBOX: remote=' in config/mailsync-*.log*
```

## How it was found

The scenario passed on the fake and failed on Dovecot with 11 ghost UIDs. The Dovecot run's
verbose log showed the background connection's `UID FETCH 1:* (UID FLAGS)` answered with all
200 rows, and `STATUS INBOX` on the same connection reporting `MESSAGES 200` after the
expunge. A raw-IMAP experiment (now `conformance/probes.py::probe_stale_fetch`) confirmed the
server behaviour, and the fake was changed to model it; the scenario then failed identically
on both.

## Root cause

Dovecot serves `FETCH` and `STATUS` from the connection's own view of the mailbox it has
selected. After another session expunges, this connection's `UID FETCH 1:*` still returns
the expunged messages' rows, and the `* n EXPUNGE` lines (or `* VANISHED` when QRESYNC is
enabled) follow the FETCH data in the same response; `STATUS` on the selected mailbox is
served from the same stale view, which Dovecot marks `[CLIENTBUG]` because RFC 3501 6.3.10
tells clients not to do it. The view is only re-synced by NOOP, IDLE, a re-SELECT or the
next command after the EXPUNGE lines were emitted.

mailsync's background connection keeps the last folder it SELECTed selected between passes
(`syncFolderChangesViaCondstore` and the plain-path new-mail fetch do not re-SELECT), so a
deep scan of that folder right after a deletion runs against the stale view. The deep scan
(`SyncWorker::syncFolderUIDRange`) then diffs a row set that still contains the deleted
messages, and mailcore's untagged EXPUNGE handling does not feed back into that diff. On the
foreground connection IDLE keeps the view current, which is why QRESYNC folders converge
promptly (`* VANISHED` arrives during IDLE) and why the same staleness re-adds the engine's
own moved copy in `stale-view-resurrects-own-move.md`.

## Fix options

1. **Sync the view before a background fetch.** A `NOOP` (or re-SELECT) on the background
   connection immediately before `syncFolderUIDRange` / the new-mail fetch makes Dovecot
   emit the pending EXPUNGE/VANISHED lines and answer the FETCH from the current view. One
   round trip per folder per pass; the simplest fix and it covers both task docs.
2. **Honour the trailing EXPUNGE/VANISHED lines.** mailcore already accumulates VANISHED
   (`takeVanishedMessages`, #141) and could do the same for sequence-number EXPUNGE by
   mapping through the session's view; then the deep scan subtracts them from the fetched
   row set. More work, and still leaves `STATUS` stale.
3. **Plan 2.9 Phase 5** (end-of-pass `UID FETCH (UID)` verification) would also catch it,
   but only at the end of the pass and only if that verification runs on a synced view.

Option 1 is recommended. Whichever is chosen, the fake models the stale view, so both
scenarios will show the fix on `fake:plain` before a Dovecot run is needed.

## Definition of done

- `plain-expunge-found-by-deep-scan` passes on `fake:plain` and `dovecot:plain` with the
  first `assert: db_matches_server` no longer marked `xfail`, and with the second
  `force_scans` removed (one scan must suffice).
- `undo-move-restores-placements` passes on `fake:plain` and `dovecot:plain` with its
  `xfail` markers removed (the sibling task).
- `python3 -m pytest test -k "qresync or deep-scan or move"` still passes: the extra NOOP
  must not change what QRESYNC connections are told (`conformance/probes.py` covers what a
  NOOP emits), and `stable` must stay clean on `remote-move-to-archive`.
