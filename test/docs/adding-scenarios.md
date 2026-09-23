# Adding a scenario: workflow, conventions and gotchas

This is the working guide for agents extending `test/`. `test/README.md` describes the
harness; this document is about the *process* of turning a provider bug or a refactor risk
into a scenario that will keep failing honestly until the engine is right, and keep passing
honestly afterwards.

## 1. What a scenario is for

A scenario pins one engine behaviour against one or more servers. It is worth writing when
one of these is true:

- a PR fixed a provider-specific bug (every fix since #121 has a reproduction that fits);
- a refactor changes a sync path and you need to know that path still converges;
- a bug report describes a sequence ("archived in webmail while Mailspring was open") that
  the engine got wrong.

It is not a unit test. It must run the real binary against a server and assert on what the
client would see (the database / delta stream), never on internal function behaviour.

## 2. Workflow

1. **Name the behaviour and its source.** Write the `description` and `source` first: which
   PR / commit / forum thread / RFC section says the server behaves this way, and what the
   engine must do about it. If you cannot cite a real server for a server-side behaviour,
   stop: a scenario built on an invented server behaviour becomes a "must support" rule for
   the engine (see §5).
2. **Choose the server kinds.** Every scenario that *can* run on Dovecot should list
   `{dovecot: qresync}` and/or `{dovecot: plain}` besides the fake, so the fake's fidelity is
   checked by the scenario itself. Fake-only is right when the behaviour needs a protocol
   hook (`at: before_fetch_body`), a personality quirk, Gmail, or SMTP.
3. **Check the harness can express it.** The `steps` vocabulary is in the README. If the
   scenario needs a server operation that does not exist (a new IMAP verb, a new quirk, a
   different notification timing), extend the fake *first* and prove it against Dovecot with
   a probe in `conformance/probes.py` (§5). Only then write the scenario.
4. **Write the YAML** (`scenarios/<kebab-name>.yaml`). Keep the mailbox small unless size is
   the point (200 messages syncs in ~4 s on the fake; 7 000 in ~30 s). Put server state that
   needs ordering into `setup:` (runs before mailsync starts) and everything that must happen
   *while the engine runs* into `steps:`.
5. **Choose expectations.** The default set is `db_matches_server` (the oracle), `counts`
   (so a wrong-but-consistent state is still caught), `stable: {passes: N}` (nothing moves on
   further passes) and `running: true`. Add `log_absent: ["UNIQUE constraint", "\\[critical\\]"]`
   when the bug was a crash. Prefer these over golden values. The derived-state invariants
   (README, "Invariants") run on every scenario without being listed.
6. **Run it on the fake, then on Dovecot**, with `--keep`, and read the artifacts once even
   when it passes: `report.txt` for the timeline, `config/mailsync-*.log` (grep `Marking`,
   `Sync loop complete`, `syncFolderUIDRange`, `Deleting`/`Unlinking`, `recv * VANISHED`)
   to confirm the engine took the path you meant to test. A scenario that passes because the
   engine never exercised the branch is worse than none.
7. **If it fails on the current engine**, decide whether that is the point. A scenario may
   document a known bug with `xfail: reason` on the affected expectations (string for all
   servers, or `{dovecot: reason}` / `{"fake:plain": reason}` per server). Write the reason
   so that someone reading `pytest -rxX` output knows what to fix. Then add a task document
   under `docs/tasks/`: symptom with the stack or log lines, the failing (scenario, server)
   pairs and a one-line repro, how it was found, the likely root cause with file:line, fix
   options, and a definition of done naming the markers to remove. Delete the document when
   the fix lands (git history keeps it; `f65f88b`..`2feef7d` hold five examples).
8. **Add it to the table in `README.md`** and, if it protects a PR, mention the scenario name
   in that PR's follow-up or in the commit that fixes the bug.

`python3 test/run.py scenarios/x.yaml --server dovecot:qresync --keep` is the fastest loop;
`python3 -m pytest test -k x` runs every server entry; `python3 test/tools/ab.py run/compare`
records outcomes for one binary and diffs two recordings (see
`docs/handoff-refactor-regression.md`).

## 3. Available scenarios

| scenario | protects | servers | status on 2026-09-20 (`d8a709d`) |
|---|---|---|---|
| baseline-initial-sync | ingestion, flags, threads, attachments, no churn | fake ×2, dovecot ×2 | pass |
| condstore-initial-sync-large | #140 chunked walk on QRESYNC, 2 500 msgs | fake, dovecot | pass |
| sparse-uid-space | #140 review: UIDNEXT 20001 / 1 100 msgs; 4 500 over a hole | fake, dovecot | pass |
| qresync-bulk-expunge-during-idle | #141 wide VANISHED during IDLE | fake, dovecot | pass |
| qresync-wide-vanished-range | #141 bounded-range query, ghost at range end | fake, dovecot | pass |
| qresync-two-expunges-during-idle | #141 second VANISHED line in one IDLE | fake, dovecot | pass |
| qresync-vanished-during-body-fetch | #141 VANISHED on a non-IDLE command | fake | pass |
| plain-expunge-found-by-deep-scan | deep-scan deletion detection | fake, dovecot | xfail: one-pass delay (stale view) |
| remote-move-to-archive | move followed once, no unpersist | fake ×2, dovecot | pass |
| remote-flag-changes | flags via IDLE and CHANGEDSINCE | fake ×2, dovecot | pass |
| new-mail-during-idle | EXISTS during IDLE; folders nobody idles on | fake ×2, dovecot | pass |
| self-addressed-inbox-and-sent | same bytes in INBOX and Sent | fake ×2, dovecot | XPASS since placements; xfail marker to remove |
| o365-duplicate-sent-copies | identical copies at adjacent UIDs | fake, dovecot | XPASS since placements; xfail marker to remove |
| proton-all-mail-duplicates | #137 `\All` skip | fake, dovecot | xfail: busy never clears |
| gateway-duplicate-list-entries | #139 duplicate LIST lines | fake | pass |
| netease-id-before-select | #121 ID before SELECT; STATUS without UIDNEXT | fake | skipped unless `/etc/hosts` maps imap.163.com |
| uidvalidity-change | UIDVALIDITY remap | fake ×2, dovecot | fake pass; dovecot xfail: segfault |
| two-folders-identical-messages | #140 non-converging gap scan must settle | fake, dovecot | XPASS since placements; xfail marker to remove |
| client-task-move | ChangeFolder / Starred / Unread tasks | fake ×2, dovecot | pass |
| client-task-trash-and-expunge | Trash + ExpungeAllInFolder | fake, dovecot | pass |
| connection-dropped-during-idle | reconnect after the server drops connections | fake, dovecot | xfail: segfault |
| send-draft | SendDraftTask over SMTP, Sent copy, self-delivery | fake +smtp ×2 | pass (failed before placements) |
| gmail-labels | All Mail + X-GM-LABELS views, archive/label/star, trash task | fake | pass |
| remote-move-destination-scanned-first | move seen "present in B" before "gone from A"; no unpersist | fake ×2, dovecot | pass |
| trash-message-with-two-placements | trash from Inbox takes the Sent copy; archive does not | fake ×2, dovecot | pass |
| flag-change-on-second-placement | per-placement flags on the Sent copy | fake ×2, dovecot | pass |
| gmail-send-and-labels | Gmail send (no Sent APPEND, one All Mail placement) + ChangeLabelsTask | fake | pass |
| migration-from-pre-placements-db | V10 migration of an 0df7864 DB + a DB caught mid-sweep | fake, dovecot | pass |
| heavy-fetch-truncation-backlog | #140 1024-header truncation + draining-backlog back-off | fake, dovecot | pass |
| modseq-truncation | CHANGEDSINCE gap > 4000 bounds the request to the newest UIDs | fake, dovecot | pass |
| mid-pass-foreground-tombstone | foreground VANISHED vs the background's stale FETCH mid-pass | fake ×2 | pass |
| trash-two-placements-without-uidplus | trash of two copies with no COPYUID (dest-fetch fallback) | fake | pass |
| undo-move-restores-placements | sourceFolderIds move and a two-copy trash, each undone via restorePlacements | fake ×2, dovecot ×2 | pass |
| undo-before-remote-phase | undo queued while the move's MOVE is held | fake ×2, dovecot | pass |
| move-into-folder-holding-a-copy | two copies in the destination after the move; undo returns the added one | fake ×2, dovecot | pass |
| mark-read-fans-out-to-all-placements | ChangeUnreadTask by threadIds hits every placement | fake ×2, dovecot | pass |
| uidvalidity-change-large-mailbox | #140 truncated UIDVALIDITY rebuild re-loops, 2 500 msgs | fake ×2 | pass |
| synced-draft-destroy / -courier | DestroyDraftTask on a synced draft and a local UID-0 draft | fake ×2, dovecot | pass |

Gaps worth filling next: iCloud / Outlook / NetEase behaviour needs recordings
(`tools/record_personality.py`) before their quirks can be asserted; `--mode test` and the
#135 EHLO fallback have no scenario; there is no live-account smoke. (Gmail send / Gmail
label tasks, migration from a pre-placements DB, the 1024-header truncation back-off and
the modseq-gap truncation are now covered by the scenarios added above.)

Harness features the placements scenarios added, worth reusing:

- **Starting on another build.** A top-level `binary: ab/mailsync-0df7864` (relative to
  `test/`) launches the scenario on that build and skips with a build hint when it is absent;
  a later `restart` (no `binary:`) lands on the build under test. This is how
  `migration-from-pre-placements-db` hands a database written by the old engine to the new
  one. `--mailsync` sets the build under test as before.
- **Doing things while the engine is stopped.** `restart: {before: [steps]}` runs server
  steps in the gap between stop and start - the honest way to build up state the engine did
  not watch happen. `modseq-truncation` uses it to make 4200 per-message flag changes at
  once (over IDLE the engine would follow them one at a time and never see a >4000 modseq
  gap). `run.py --server kind:profile` uses the scenario's own entry for that kind:profile,
  so its options (`smtp: true`, `smtp_sent_copy`, `imap_host`) and the top-level `binary:`
  apply; a kind:profile the scenario does not list runs bare.
- **`per_message: true` on `server.flags`** issues one STORE per UID, so HIGHESTMODSEQ
  advances once per message. A plain `server.flags` is one STORE and one modseq bump, which
  is what Dovecot does and what `conformance/probe_bulk_store_modseq` now pins.
- **`log_count: {regex: {min, max}}`** bounds how many times a log line appears, which is how
  `heavy-fetch-truncation-backlog` proves the truncated deep scan retried a bounded number of
  times instead of looping. It counts the whole run, initial sync included: a folder under
  5 000 messages is initially swept as one `1:*` range and already produces truncated
  fetches, so count those in (`uidvalidity-change-large-mailbox`).
- **Targeting one connection with a hook.** `at: {hook: before_command, session: background,
  command: "^UID FETCH 1:\\*", mailbox: INBOX, delay: 1.5}` fires only on the connection
  that has never idled, for that command line, with that mailbox selected; `delay` holds the
  reply so the other connection (the idling foreground, which gets VANISHED at once) acts
  first. That is how `mid-pass-foreground-tombstone` produces the stale-FETCH race
  deterministically. The engine's background connection can be told apart from the
  foreground only by `has_idled`; the report line names which one the hook hit.
- **Undoing a task.** `client.undo_task: {of: label}` reads the completed task's row from the
  `Task` table and queues its undo the way `UndoRedoStore` + `createUndoTasks()` do (same
  class and ids, `isUndo`; for `ChangeFolderTask` the engine-written `undoPlacements` copied
  to `restorePlacements`, `sourceFolderIds` set to the original destination and `folder` to
  the first recorded source). `sourceFolders: [INBOX]` on the original task is the
  perspective's folder (`sourceFolderIds`).
- **Thread-level tasks.** `threads: {mailbox, uids}` resolves to the distinct `threadIds` of
  those messages, which is how the client sends ChangeUnread/ChangeStarred/ChangeFolder.
- **Local-only rows.** `messages: {header_message_ids: [...]}` addresses a row the placements
  view cannot see (a draft saved by SyncbackDraftTask sits at UID 0 in Drafts). Note that
  `SyncbackDraftTask` has no remote phase - the engine never APPENDs drafts - so "a draft
  with a UID on the server" has to be put there by another client (`mailboxes: {Drafts:
  {messages: 2, flags: ["\\Draft", "\\Seen"]}}`).
- **Personalities that strip a capability**: `{fake: dovecot-without-uidplus}`. The fake
  omits COPYUID/APPENDUID when UIDPLUS is not advertised (RFC 4315), which is what drives the
  engine's find-the-copy-in-the-destination fallback (`trash-two-placements-without-uidplus`).
  Dovecot cannot be configured without UIDPLUS (the `plain` profile only changes the
  advertised string), so this one has no Dovecot control.

## 4. Gotchas (each of these cost real time)

Engine interface

- **The binary's path must contain "mailspring"** (release builds exit 2 silently otherwise).
  The harness launches through a symlink, but if you run it by hand from `/tmp/x/mailsync`
  it will look like an instant hang.
- **Run `--mode migrate` before every `--mode sync`**, as the client does - not just on a
  fresh CONFIG_DIR (where skipping it makes every thread race to create the schema and abort
  with "database is locked"), but also before a `restart` onto a newer build, which would
  otherwise run against tables it does not have. `MailsyncProcess.start()` does it on every
  launch and the report records what migrate said.
- **Task JSON uses `aid`**, not `accountId`, plus `v`, `status`, `metadata: []`;
  `queue_task()` fills them in. A `null` where the engine expects a string aborts the whole
  process (`catch (...) { abort(); }` in the stdin loop) - it looks like a crash, but it is
  your JSON. Drafts additionally need `aid`, `v`, `date` inside `draft`.
- **`--verbose` is required** for foreground-idle detection (it reads `sent N IDLE` from the
  log) and for the IMAP transcript you will want anyway. It rotates the log every 5 MB; the
  tailer follows `.log.1`.
- **Skipped folders never clear `busy`** (the `\All` mailbox): use `quiescence:
  {ignore_busy: [...]}` in the scenario or the wait never ends.
- **There is no "sync finished" signal.** Quiescence is inferred (README). If a wait times
  out, `describe()` in the report says which condition was unmet
  (`waiting because: ...`); usually it is a folder left busy or the background worker looping.
- The background worker sleeps 120 s between passes; `sync: pass` sends `wake-workers`.
  Deep/shallow/gap scans are gated on `lastDeep`/`lastShallow` timestamps; `force_scans`
  backdates them in the DB (the stopgap until the engine reads intervals from the environment).
- On Gmail, the engine detects Gmail by the `X-GM-EXT-1` capability, not by `provider`, so
  use `provider: imap` (the default) - `provider: gmail` would try OAuth against Google.
- Hostname-gated branches (iCloud `imap.mail.me.com`, NetEase `imap.163.com`, Outlook) need
  the name to resolve to 127.0.0.1; declare `imap_host` on the server entry and the scenario
  skips with instructions when it does not.

Timing

- **Do not race Dovecot's IDLE delivery.** Notifications arrive ~0.5 s after a change. A
  `sync: pass` sends `wake-workers`, which interrupts the foreground IDLE; if that happens
  before the notification lands, the engine handles it a cycle later and your expectations
  run too early. After a server change on a QRESYNC server, `wait: {log: "recv \\* VANISHED"}`
  (or the matching `EXISTS`/`FETCH` line) before `sync: pass`.
- **A burst of changes** made within milliseconds is reported by Dovecot only partly during
  IDLE and the rest after DONE. Space server operations by ~1 s when the order matters.
- **Dovecot answers FETCH/STATUS from the session's stale view** after another session's
  expunge (rows still returned, EXPUNGE lines after the data). The engine's deep scan sees
  deletions one pass late on plain servers; this is real and is modelled in the fake.
- **Deferred deletion is not churn.** The engine deletes a vanished copy at once but keeps a
  message that lost its last copy (an orphan) until the end of the next pass, so `unpersist`
  deltas can appear on an "idle" pass. `stable` only counts a message that was placed at the
  start of the pass and got deleted during it.
- Dovecot's `mailbox_idle_check_interval` is set to 2 s in the harness config; the 30 s default
  is not what causes the burst behaviour.
- **Background folder order is a role sort**: `inbox, sent, drafts, all, archive, trash,
  spam`, then unroled folders in database order (`SyncWorker::syncNow`, `roleOrder`). To
  make folder B scanned before folder A, give B an earlier role or move into a role-earlier
  folder. The foreground connection idles on the inbox and sees inbox changes first
  regardless of this order.
- **Changes the engine did not watch happen** (thousands of flag changes while the app was
  closed, a mailbox rebuilt overnight) cannot be produced while it runs: over IDLE it follows
  them one at a time and never sees a large gap. Use `restart: {before: [server steps]}`,
  which applies the steps between stop and start (`modseq-truncation`).
- With `--verbose` the engine log rotates at 5 MB to `mailsync-*.log.1`, `.log.2`. The
  harness tailer follows the rotation, but when you grep by hand use
  `config/mailsync-*.log*` or the line you are looking for may be in a rotated file.

Data

- **Generate unique messages.** `mailgen` gives every message a unique Message-ID, subject
  and date; the engine hashes headers into the row id, so two different messages with the
  same headers become one row and *flap* between folders - which looks exactly like the bug
  you are hunting. Duplicates are explicit: `duplicate_of:` / `server.duplicate`.
- **Dates matter.** The engine prefetches bodies only for recent messages; `mailgen` dates are
  recent by default, use `age_days:` to make a folder old (e.g. to make `need-bodies` fetch).
- **The engine tracks only `\Seen`, `\Flagged`, `\Draft`.** Comparing `\Deleted` or `\Answered`
  is meaningless; `assertions.TRACKED_FLAGS` filters.
- Mailbox names come from the personality: the Outlook personality's Sent is "Sent Items",
  Courier's is "INBOX.Sent"; a scenario written for `Sent` fails at setup on those.
- `db_matches_server` compares the placements view; on Gmail pass `labels: true` and note
  that only All Mail / Spam / Trash are physical on the server side.
- `harness/db.py` is the only schema-aware code: it reads `MessageFolder` when present and
  the old `Message.remoteFolderId/remoteUID` columns otherwise, excluding the old engine's
  unlink sentinels (`remoteUID = UINT32_MAX - phase`). If the placements schema changes,
  change `_placements_from_join_table` and nothing else.

Servers

- The fake runs in-process: `server.<op>` steps mutate the store directly and notifications
  flow to connected sessions. `at:` hooks run on the *session's* thread when that protocol
  moment occurs; they are fake-only and are applied immediately on Dovecot with a note.
- Dovecot mutations go through an `imaplib` session that SELECTs the mailbox (claiming
  `\Recent`, like any client would); the fake adapter mirrors that so both report alike.
- `set_uidvalidity` renumbers UIDs on the fake (a rebuilt mailbox); on Dovecot `doveadm
  mailbox update --uid-validity` changes the value without renumbering *and drops the
  session*, which currently segfaults the engine.
- Docker on Apple Silicon: the official `dovecot/dovecot` image is amd64-only and its child
  processes die under emulation; the harness builds `mailsync-harness-dovecot:2.3.21` from
  Alpine's package. Agent containers without Docker: `apt install dovecot-imapd` and
  `HARNESS_DOVECOT_MODE=local`.
- Random ports everywhere; two harness runs can coexist, but they compete for CPU and the
  timing rules above get tighter. Do not run two Dovecot suites at once on a laptop.
  Artifacts go to `test/runs/session-<pid>/<scenario>-<server>/`, which a second session running the same
  scenario would delete: set `HARNESS_RUNS_DIR` per session (`ab.py` does this itself).
- `conformance/probe_idle` compares what an idling session was told; Dovecot's IDLE timing
  makes it flake roughly one run in ten. Re-run it before treating a difference there as real;
  a difference in any other probe is real.

## 5. Extending the fake without inventing a server

`fakeimap/` is only trustworthy because `conformance/` proves its default behaviour against
Dovecot for every exchange mailsync makes. When you need the fake to do something new:

1. If it is *baseline* IMAP behaviour, add a probe to `conformance/probes.py` that performs
   the exchange, run `python3 test/conformance/compare.py <probe-name> -v`, and make the fake
   match Dovecot's output. Normalize only what legitimately differs (UIDVALIDITY, modseq
   values, timings, human text); if you find yourself normalizing structure, the fake is wrong.
2. If it is a *provider quirk*, add it to a `Personality` in `fakeimap/personalities.py` with
   a `quirks` entry whose value is the citation (PR, forum thread, RFC, upstream commit) and
   gate the behaviour in `server.py` on `self.p.has("quirk-name")`. Never make a quirk the
   default. If the exact wire format is reconstructed from reports rather than a captured
   transcript, say `NEEDS-RECORDING` in the personality and do not assert on the text.
3. If it is a *real provider's* behaviour you can reach, record it first:
   `IMAP_PASSWORD=... python3 test/tools/record_personality.py host 993 user --name x`,
   then build the personality from the recording.
4. Run the whole conformance suite afterwards (`python3 -m pytest test/conformance`);
   a change to notification ordering or flush behaviour tends to move more than one probe.

What the first draft of this fake got wrong, all caught by conformance and worth knowing:
attribute order in FETCH responses, `.SILENT` still reporting MODSEQ, no-op STOREs reporting
nothing, `[HIGHESTMODSEQ]` on tagged OKs after VANISHED, coalesced `EXISTS`, per-session
`RECENT`, `[CLOSED]` on re-SELECT, `\Noselect` parent creation, `VANISHED (EARLIER)` being
re-sent on repeated CHANGEDSINCE, the stale FETCH view, and `X-GM-LABELS` quoting.

## 6. Proposing a scenario in a PR or task

A proposal is a filled-in header - `name`, `description`, `source`, `servers` - plus the
step list in prose, and the answer to "what does the engine get wrong today, and how will
the expectation catch it?". If the scenario needs the fake to change, say which probe will
prove the change. Reviewers should be able to tell from the YAML alone which engine branch is
exercised and which real server behaviour justifies each server-side step.
