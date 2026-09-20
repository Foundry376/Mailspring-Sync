# Handoff: before/after regression testing for the message-placements refactor

For the agent working on the `MessageFolder` placements rework (commits `3193b2f..48f0304`
and whatever follows). This explains how to prove the refactor has not regressed the
provider-specific behaviour the engine accumulated over the last year, using the integration
suite under `test/`.

## What you have

- **A black-box suite** (`test/README.md`): 23 scenarios that run a real `mailsync` binary
  against a scriptable fake IMAP server and against Dovecot 2.3.21 in Docker, mutate the
  server while the engine runs, queue tasks on stdin, and assert that the engine's database
  matches the server. Each scenario cites the PR it protects (#121, #137, #139, #140, #141
  and the task/send paths). Nothing in it depends on engine internals except one adapter,
  `test/harness/db.py`, which already reads `MessageFolder` (and falls back to
  `Message.remoteFolderId/remoteUID` when the table does not exist), so **the same
  scenarios run against both sides of the refactor**.
- **A pre-refactor binary and its results.** `test/ab/mailsync-0df7864` is a Release
  universal build of `0df7864` (the last commit before placements), and
  `test/ab/before-0df7864.json` holds its outcome for every (scenario, server) pair.
  `test/ab/` is gitignored; if the files are gone, §4 rebuilds them in ~10 minutes.
- **A comparison tool**, `test/tools/ab.py`, that records a run and diffs two recordings.

## 1. The loop

```bash
# once per engine change you want to evaluate: build, then
python3 test/tools/ab.py run --mailsync ../app/mailsync --out test/ab/after.json
python3 test/tools/ab.py compare test/ab/before-0df7864.json test/ab/after.json
```

`run` takes ~25 min with Docker (fake + Dovecot), ~12 min with `--servers fake`. It prints
one line per case as it goes and writes the JSON at the end. `compare` prints every case
whose outcome changed, marks regressions, shows the failure text for them, and exits 1 if
there is any regression. Outcomes:

| outcome | meaning |
|---|---|
| PASS | every expectation held |
| FAIL | an expectation failed (the detail says which, and what differed) |
| XFAIL | only expectations marked `xfail` failed - a known engine bug behaved as expected |
| XPASS | an `xfail` expectation now passes: the known bug is fixed; remove the marker |
| SKIP | server kind unavailable, or a hostname alias is missing |
| ERROR | the harness itself failed (not an engine verdict) |

A regression is PASS→FAIL/ERROR, XPASS→FAIL, or PASS→XFAIL. XFAIL→XPASS is what the refactor
is *supposed* to produce for the placements scenarios (see §2).

To iterate on a single case:

```bash
python3 test/run.py test/scenarios/remote-move-to-archive.yaml --server dovecot:qresync --keep
# then read test/runs/<name>/report.txt, config/mailsync-*.log (grep Tombstoning|Unlinking|
# syncFolderUIDRange|Marking|Sync loop complete|recv \* VANISHED), config/edgehill.db
```

## 2. What the baseline says, and what the refactor changed

Recorded 2026-09-20 with `test/tools/ab.py` (both files are in `test/ab/`, the diff in
`test/ab/compare-0df7864-vs-d8a709d.txt`):

| | `0df7864` (before) | `d8a709d` (placements) |
|---|---|---|
| PASS | 33 | 35 |
| XFAIL (known bug behaved as expected) | 14 | 7 |
| XPASS (known bug gone; marker to remove) | 0 | 7 |
| FAIL | 2 | 0 |
| SKIP (needs `/etc/hosts` alias) | 1 | 1 |

`ab.py compare`: **41 unchanged, 9 changed, 0 regressions.** The nine changes are all the
cases the refactor exists for:

```
fixed  o365-duplicate-sent-copies[dovecot:qresync]:      XFAIL -> XPASS
fixed  o365-duplicate-sent-copies[fake:dovecot]:         XFAIL -> XPASS
fixed  self-addressed-inbox-and-sent[dovecot:qresync]:   XFAIL -> XPASS
fixed  self-addressed-inbox-and-sent[fake:dovecot]:      XFAIL -> XPASS
fixed  self-addressed-inbox-and-sent[fake:plain]:        XFAIL -> XPASS
fixed  send-draft[fake:dovecot]:                         FAIL  -> PASS   (self-addressed copy in INBOX and Sent)
fixed  send-draft[fake:plain]:                           FAIL  -> PASS
fixed  two-folders-identical-messages[dovecot:qresync]:  XFAIL -> XPASS
fixed  two-folders-identical-messages[fake:dovecot]:     XFAIL -> XPASS
```

Two things to do with that:

- **Remove the now-wrong `xfail` markers** (`db_matches_server` in
  `self-addressed-inbox-and-sent.yaml`, `o365-duplicate-sent-copies.yaml`,
  `two-folders-identical-messages.yaml`) so a later regression in exactly these cases fails
  loudly instead of quietly reverting to XFAIL. `ab.py` treats XPASS->XFAIL as a regression,
  but pytest alone would not.
- **Re-record `after` on every engine change** you want to evaluate and compare against
  `before-0df7864.json`; do not re-record the baseline unless the scenarios change (then
  re-record both, so the comparison is like for like).

Still XFAIL on both sides, and *not* yours: `connection-dropped-during-idle` and
`uidvalidity-change` on Dovecot (segfaults in the #141 VANISHED accumulator,
`docs/tasks/vanished-accumulator-segfault.md`), `proton-all-mail-duplicates` (`busy` never
cleared, `docs/tasks/all-mail-busy-never-clears.md`), and `plain-expunge-found-by-deep-scan`
(one-pass delay caused by Dovecot's stale session view, §3). Leave them xfail unless you fix
them; if one flips to XPASS in your run, say so in the PR, it means a side effect worth
understanding.

## 3. Things learned about servers that bear on the placements design

- **Dovecot answers `FETCH` and `STATUS` from the session's own view.** A connection that had
  INBOX selected before another session expunged messages still gets those messages back
  from `UID FETCH 1:*`, followed by the `EXPUNGE`/`VANISHED` lines *after* the FETCH data,
  and `STATUS INBOX` on that connection reports the old counts (Dovecot tags it
  `[CLIENTBUG]`). mailsync's background connection keeps the last folder it touched selected,
  so a deep scan right after a deletion sees the old row set. Your sweep/tombstone logic must
  not treat "the FETCH listed it" as "it is on the server" when a VANISHED/EXPUNGE arrived in
  the same response; and a placement re-upserted from such a FETCH would resurrect a
  tombstone. The fake models this (`conformance/probes.py::probe_stale_fetch`).
- **Dovecot re-reports `VANISHED (EARLIER)`** for everything since the requested modseq, every
  time, including UIDs the session was already told about. Tombstoning must be idempotent.
- **IDLE delivery is ~0.5 s late** and a `wake-workers` that interrupts IDLE first defers the
  notification to the next cycle. Scenarios wait for `recv * VANISHED` before forcing a pass;
  if you write new ones, do the same.
- **Deferred deletion is expected.** `stable` (the flapping detector) ignores `unpersist`
  deltas for rows that were already unplaced at the start of the pass, so your end-of-pass
  sweep does not read as churn. It *does* count a placed message being deleted, or any
  (folder, UID) placement appearing/vanishing on a pass with no server change.

## 4. Rebuilding the baseline

```bash
S=$(mktemp -d /tmp/mailspring-baseline.XXXX)      # path must contain "mailspring"
git archive 0df7864 | tar -x -C $S
( cd $S && xcodebuild -scheme mailsync -configuration Release -destination 'generic/platform=macOS' \
    -derivedDataPath $S/dd CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO )
# the last build phase fails copying to ../app (absent here); the binary is fine:
cp $S/dd/Build/Products/Release/mailsync test/ab/mailsync-0df7864
python3 test/tools/ab.py run --mailsync test/ab/mailsync-0df7864 --out test/ab/before-0df7864.json
```

On Linux: cmake per `BUILDING.md` in the exported tree (`-DCMAKE_CXX_STANDARD_LIBRARIES="-ldb"`
if libetpan links against Berkeley DB), then the same `ab.py run`. Using a separate
`-derivedDataPath` keeps the build from touching the working tree's DerivedData.

## 5. What you should add

The catalog was written against the old model; these placements-specific behaviours have no
scenario yet and are the ones a regression would hide in:

1. **Tombstone sweep timing** - a message moved by another client is seen as "gone from A"
   and "present in B" in either order across two passes; assert it is never deleted and
   never duplicated (`deltas: {Message: {unpersist: 0}}`, `stable`). `remote-move-to-archive`
   covers the common order; add the reverse (Archive scanned before INBOX) by listing
   folders so Archive sorts first, or by moving into a folder the background worker visits
   earlier.
2. **`ChangeFolderTask` with `sourceFolderIds[]`** and `undoPlacements` - move one copy of a
   message that has two placements, undo, assert both server and DB.
3. **Trash/spam moves all placements** - a self-addressed message (INBOX + Sent) trashed from
   INBOX: `server_counts` must show it gone from both.
4. **Gmail exclusivity** - one placement in All Mail; a label change must not create a second
   placement; `gmail-labels` covers the basics, add label tasks (`ChangeLabelsTask` with
   `labelsToAdd: ["Work"]`) and a Gmail `send-draft` (no Sent APPEND; All Mail placement).
5. **Migration** - covered by `migration-from-pre-placements-db`: a scenario starts on the
   build under test unless it sets a top-level `binary:`; that scenario sets
   `binary: ab/mailsync-0df7864`, syncs, expunges on the server so the old engine leaves
   unlinked rows mid-sweep, and a bare `restart:` lands on `--mailsync` (the build under test),
   which must migrate and converge. It skips with a build hint when the baseline binary is
   absent (§4), so it is an ab-style check rather than a CI one.

`test/docs/adding-scenarios.md` has the workflow and the gotchas; the fake must not be
taught new server behaviour without a conformance probe against Dovecot.

## 6. Engine changes the harness is waiting for

You are the one editing the engine, so these are yours to land when convenient:

- Environment overrides for the scan intervals (`DEEP_SCAN_INTERVAL`, `SHALLOW_SCAN_INTERVAL`,
  `CONDSTORE_GAP_SCAN_INTERVAL`, `CACHE_CLEANUP_INTERVAL`, and the 120 s worker sleep in
  `main.cpp`), e.g. `MAILSYNC_DEEP_SCAN_INTERVAL=1`. The harness currently backdates
  `lastDeep`/`lastShallow` in `Folder.localStatus` (`force_scans`) to trigger scans, which is
  a write to a database the engine owns. Once the variables exist, `force_scans` becomes
  "set env + wake".
- Optionally `MAILSYNC_DISABLE_DAV=1` to keep CardDAV discovery out of test logs, and one
  `logger->info("Sync quiescent")` when the foreground enters IDLE with no queued work.
- If `MessageFolder` columns change, update `_placements_from_join_table` in
  `test/harness/db.py` in the same commit; it is 20 lines.
