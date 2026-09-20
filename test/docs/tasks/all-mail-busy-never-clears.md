# Task: the skipped `\All` mailbox stays `busy` forever

Status: open. Found 2026-09-19 by the integration harness. Present on `0df7864` and on the
placements branch.

## Symptom

On any non-Gmail account with an `\All` mailbox (ProtonMail Bridge's "All Mail", or any
server advertising RFC 6154 `\All`), the folder's `localStatus.busy` is set to `true` at
launch and on every `wake-workers` (the client's "Sync Mail" button / wake from sleep) and is
never set back to `false`. In the client that folder shows a sync spinner indefinitely, and
anything that waits for "all folders idle" (the harness's quiescence check, and any client
logic keyed on `busy`) never settles.

## Failing test

- `test/scenarios/proton-all-mail-duplicates.yaml`, expectation
  `folder_status: {"All Mail": {busy: false}}`, marked `xfail` on `fake:proton-bridge` and
  `dovecot:proton-like`. The scenario declares `quiescence: {ignore_busy: ["All Mail"]}` so
  the rest of it can run; remove that too once fixed.

```bash
python3 test/run.py test/scenarios/proton-all-mail-duplicates.yaml --server dovecot:proton-like --keep
sqlite3 test/runs/proton-all-mail-duplicates-dovecot-proton-like/config/edgehill.db \
  "select path, json_extract(data,'$.localStatus.busy') from Folder"
```

## How it was found

The scenario's `wait: quiescent` timed out after 180 s with every folder idle except
`All Mail: {"busy": true, "syncedMinUID": 1}` while both workers were asleep. The first run
showed `busy` absent (the folder was never touched); after the first `wake-workers` it was
`true` and stayed so.

## Root cause

`SyncWorker::markAllFoldersBusy()` (SyncWorker.cpp:303) sets `busy = true` on every folder
of the account at launch and whenever `bgWorkerShouldMarkAll` is set by `wake-workers`. The
`isDuplicateAllMail` branch in `SyncWorker::syncNow()` (SyncWorker.cpp:417-427, from
8c63878 / PR #137) then writes `lastShallow`, `lastDeep`, `bodiesWanted`, `syncedMinUID`,
`uidnext` and `messageCount` "to mimic the folder being fully synced" - but not `busy`.
Every other folder gets `busy = false` at the end of its normal sync path, which the skipped
folder never reaches.

## Fix

Set `localStatus[LS_BUSY] = false` in the skip branch alongside the other keys (and confirm
`saveFolderStatus` persists it - the branch already saves the folder). One line; the
scenario's `folder_status` expectation is the regression test.

## Definition of done

- `proton-all-mail-duplicates` passes on both servers with the `xfail` and the
  `quiescence.ignore_busy` entry removed.
- `MailsyncProcess.wait_quiescent()` in `test/harness/mailsync.py` can drop its
  `ignore_busy` parameter, or keep it for other skipped folders - reviewer's call.
