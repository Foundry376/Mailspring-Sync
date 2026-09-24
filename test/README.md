# Sync engine integration tests

Black-box tests that run a real `mailsync` binary against a mail server in a known state,
let it sync, optionally change things on the server or queue tasks on stdin, and then check
that the engine's database says what the server says. No C++ is compiled for these; the
engine is exercised exactly as the Mailspring client exercises it.

```
test/
  harness/        drives mailsync, reads its database/log/delta stream, runs scenarios
  fakeimap/       an in-process, scriptable IMAP server with server "personalities"
  servers/        Dovecot 2.3.21 configuration + Dockerfile
  scenarios/      one YAML file per scenario
  conformance/    proves the fake answers like Dovecot for everything mailsync sends
  tools/          ab.py (before/after regression comparison), record_personality.py
  docs/           adding-scenarios.md (workflow + gotchas), handoff-refactor-regression.md;
                  tasks/ holds write-ups of engine bugs the suite finds until they are fixed
  runs/           per-run artifacts (gitignored): engine log, DB, server transcript
  ab/             gitignored: baseline binaries and ab.py recordings
```

Start with `docs/adding-scenarios.md` if you are here to add a test, and
`docs/handoff-refactor-regression.md` if you are here to check a refactor for regressions.

## Running

```bash
pip install pytest pyyaml            # the only dependencies beyond the standard library
python3 -m pytest test               # scenarios on every available server kind + conformance
python3 -m pytest test --servers fake            # fake only (no Docker needed, ~5 min)
python3 -m pytest test -k "qresync" --servers dovecot
python3 test/run.py test/scenarios/qresync-bulk-expunge-during-idle.yaml --server dovecot:qresync --keep
python3 test/conformance/compare.py -v          # fake vs Dovecot, all probes, full diffs
```

The binary comes from `MAILSYNC_BIN`, else `../app/mailsync`, else the Linux cmake output
at the repo root, else Xcode's DerivedData. `--mailsync PATH` overrides per run. Release
builds refuse to start unless argv[0] contains "mailspring"; the harness launches through a
symlink so this never bites.

Dovecot runs in Docker (image built on first use from `servers/dovecot/Dockerfile`), or,
where Docker is unavailable but `dovecot`/`doveadm` are installed (`apt install
dovecot-imapd` in an agent container), as a local process with a private config
(`HARNESS_DOVECOT_MODE=local|docker`). `HARNESS_SERVERS=fake,dovecot` forces the set.

`run.py --server kind:profile` uses the scenario's entry for that kind:profile, so its
options (`smtp: true`, `imap_host`, ...) and a top-level `binary:` apply. Every
process gets its own `test/runs/session-<pid>/`, so concurrent sessions never delete each
other's artifacts; `HARNESS_RUNS_DIR` overrides the location.

Failed runs keep their artifacts in `test/runs/session-<pid>/<scenario>-<server>/`: `report.txt` (what
happened, when), `config/mailsync-*.log` (engine log; with `--verbose` it includes every
IMAP line sent and received, per thread), `config/edgehill.db`, `server.log` (the fake's
transcript). `--keep` / `HARNESS_KEEP=1` keeps passing runs too.

## How a scenario works

```yaml
name: qresync-bulk-expunge-during-idle
source: Mailspring-Sync PR #141          # the bug or behaviour this protects
servers: [{fake: dovecot}, {dovecot: qresync}]
mailboxes:
  INBOX: {messages: 200}
steps:
  - wait: quiescent
  - server.expunge: {mailbox: INBOX, uids: "140:195"}
  - wait: {quiescent: true}
  - sync: pass
expect:
  db_matches_server: {}
  counts: {INBOX: 144}
  stable: {passes: 1}
  running: true
```

Each `servers` entry becomes one pytest case. `mailboxes` populates the server before
mailsync starts (`messages:` is a count or `{count, html, attachment, thread, self_addressed,
age_days, ...}` for `harness/mailgen.py`; `flags:`; `duplicate_of: {mailbox, uids}` for
byte-identical copies). `setup:` steps run before mailsync starts.

Steps:

| step | meaning |
|---|---|
| `wait: quiescent` / `wait: {quiescent: true, timeout: s}` | until the engine has nothing to do (see below) |
| `wait: {seconds: n}`, `wait: {log: regex}`, `wait: {task: label}` | |
| `sync: pass` | `wake-workers` on stdin, then wait for that pass to finish |
| `server.expunge / flags / move / copy / duplicate / append / create_mailbox / set_uidvalidity / set_uidnext / drop_connections / pause` | what another client does to the mailbox (`pause` changes nothing: with `at` and `delay` it holds one reply, as in `undo-before-remote-phase`); `at: before_fetch_body|idle_start|idle_tick|before_command` defers it to that protocol moment (fake only) |
| `at: {hook, session: foreground|background, command: regex, mailbox, delay: s}` | the same, narrowed to one connection (`foreground` = the one that has idled), one command line (`"^UID FETCH 1:\\*"`) and one selected mailbox; `delay` holds that connection's reply so another connection acts on the change first (`mid-pass-foreground-tombstone`) |
| `server.reject: {at: {hook: before_command, command: regex}, code, text}` | the hooked command is answered `NO [code] text` instead of being run (fake only; commands without literals), as in `move-rejected-by-server` |
| `at: {..., every: true}` | keep the hook armed instead of firing once, e.g. a folder whose STATUS fails on every pass (`orphan-sweep-with-unreadable-folder`) |
| `server.flags: {..., per_message: true}` | one STORE per UID, so HIGHESTMODSEQ advances once per message - how a modseq gap larger than one grows on a real server (used by `modseq-truncation`) |
| `client.task: {__cls: ChangeFolderTask, messages: {mailbox, uids}, folder: Archive}` | `Actions.queueTask` on stdin; `messages` resolve to engine ids (`{mailbox, uids}`, or `{header_message_ids: [...]}` for rows without a server placement such as a local draft), `threads: {mailbox, uids}` to their `threadIds`, `folder`/`labelsTo*` to Folder JSON, `sourceFolders: [paths]` to `sourceFolderIds` |
| `client.undo_task: {of: label}` | queue the undo of a completed task from its stored data, as `UndoRedoStore` does: for a `ChangeFolderTask` the engine-written `undoPlacements` become `restorePlacements` and the original destination its `sourceFolderIds` |
| `client.need_bodies`, `client.wake` | the other stdin commands |
| `force_scans: {}` | backdate `lastDeep`/`lastShallow` in the DB and wake (see Stopgaps) |
| `restart: {binary: path, before: [steps]}` | stop and relaunch on the same database, optionally with another build; `before:` runs steps while the engine is stopped (server state it did not watch happen) |
| `snapshot: name`, `assert: {...}` | mid-scenario checkpoints |

A scenario may also start on another build with a top-level `binary: ab/mailsync-0df7864`
(relative to `test/`) and `restart` onto the build under test - how a database written by an
older engine is handed to the current one (`migration-from-pre-placements-db`). `--mailsync`
still sets the build under test that a `restart` with no `binary:` lands on. The harness runs
`--mode migrate` on every launch, as the client does, so a restart onto a newer build
upgrades the schema.

A top-level `env: {NAME: value}` is added to the engine's environment on every launch, for
the knobs the engine reads from it: `ORPHAN_SWEEP_MAX_WAIT` (seconds; default one day) is how
long the orphan sweep waits for a folder that has not been fully scanned.

Expectations: `db_matches_server` (placements: every (folder, UID) on the server is a
message locally with the same Message-ID and tracked flags, and nothing local is missing on
the server), `counts`, `shown` (messages per folder as the client sees them: the
`Message.folders` snapshot, so a copy in flight counts in its pending destination), `server_counts`, `server_has: {mailbox: [Message-IDs]}` (those messages are in that mailbox on the server, whatever the engine believes), `stable: {passes: N}` (N more passes over an
unchanged mailbox must not move a single placement - the flapping detector), `folder_status`,
`log_present` / `log_absent`, `log_count: {regex: n}` or `{regex: {min, max}}` (how many
log lines match, to bound a loop the engine should take a known number of times),
`deltas: {Message: {unpersist: 0}}`, `unchanged_since: snapshot`, `running`, `exit`. Any
expectation may carry `xfail: reason` for a known engine bug: it is recorded, not failed,
and reported as XPASS once it starts passing.

**Invariants** run after every scenario's expectations, on every server, without being
listed (`harness/invariants.py`). Once the engine is quiescent they recompute each derived
layer of the engine's state from the stored layer below it, so a bug is reported once, where
it starts: `message_snapshot` (`Message.folders` / `labels` from `MessageFolder` rows),
`message_flags` (message unread / starred / draft from its rows, JSON vs indexed columns),
`thread_refcounts` (thread folder and label `_refs` / `_u`, unread / starred, inAllMail from
its messages' snapshots), `thread_categories` (`ThreadCategory` from the thread's arrays),
`thread_counts` (`ThreadCounts` from `ThreadCategory`) and `orphans` (`MessageOrphan` lists
exactly the messages with no `MessageFolder` row, which is how the end-of-pass sweep finds them).
A scenario that trips one because of a known engine bug says so rather than skipping it
silently: `expect: {invariants: {skip: [thread_counts]}}` with a comment naming the bug,
`invariants: {xfail: reason}`, or `invariants: false` to disable all of them. They also work
mid-scenario as `assert: {invariants: {}}`.

**Quiescence** is inferred, since the engine has no "done" signal: every Folder's
`localStatus.busy` is false and `syncedMinUID <= 1`; the background thread's last log line
is `Sync loop complete.` and older than the settle window (it loops without sleeping while
there is more to do); the foreground thread's last IMAP command is `IDLE`; and no delta has
arrived within the settle window.

**The oracle.** `harness/db.py` is the only code that knows the engine's schema. It turns
`Message.remoteFolderId/remoteUID` (or `MessageFolder` rows, once they exist) into
`{folder: {uid: Placement}}`, and `Server.truth()` reads the same shape back from the server
over IMAP. Everything else compares those two, which is what lets the scenarios survive the
placements refactor: only `placements_from_db` changes.

## The fake server, and keeping it honest

`fakeimap/` is a stateful IMAP4rev1 server with CONDSTORE, QRESYNC, IDLE, UIDPLUS, MOVE,
ENABLE, ID, NAMESPACE, SPECIAL-USE, STARTTLS and the Gmail extensions (plus a small SMTP
server), driven either in-process (the
harness calls `store.expunge(...)` and the notifications flow to connected sessions exactly
as they would from another client) or at protocol hooks. Sequence numbers are per session;
a session is told about an expunge once; modseqs behave like Dovecot's.

A flaw in the fake would become a "must support" condition on the engine, so the rules are:

1. **The default personality (`dovecot`) is Dovecot 2.3.21**, and `conformance/` proves it.
   `conformance/probes.py` sends every exchange mailsync performs (session setup, LIST /
   STATUS, SELECT / EXAMINE, header and body FETCH shapes, CHANGEDSINCE with and without
   VANISHED, STORE incl. `.SILENT`, COPY / MOVE / APPEND, EXPUNGE, CREATE / RENAME / DELETE,
   and what an idling session is told) to both servers from identically populated state and
   diffs the normalized responses. 56 comparisons currently match exactly; a difference is a
   failing test unless it is listed in `ALLOWED_DIFFERENCES` with a reason. Run it after any
   change to `fakeimap/`.
2. **Every deviation from that baseline is a named quirk on a `Personality`** in
   `fakeimap/personalities.py`, with a citation: the PR, forum thread, RFC section or upstream
   commit that shows a real server doing it. Personalities whose exact wire format was
   reconstructed from reports rather than a captured transcript say `NEEDS-RECORDING`; a
   scenario may exercise the engine's branch for them, but their text should be replaced
   with output from a real session (`tools/record_personality.py`, to be written) before
   anything is asserted about that text.
3. **Generated messages are unique** (Message-ID, subject, date) unless a scenario asks for
   duplicates via `duplicate_of` / `server.duplicate`. The first version of this harness
   gave every mailbox `<msg{uid}@example.test>`, so INBOX's message 1 and Archive's message 1
   collapsed onto one row and flapped between folders - a fake-server artefact that looked
   exactly like a real bug.

Things learned from Dovecot while building the conformance suite, all now modelled:

- Attribute order in unsolicited/STORE FETCH responses is `UID`, `MODSEQ`, `FLAGS`; a
  `.SILENT` store still reports `MODSEQ` when CONDSTORE is enabled; a STORE that changes
  nothing reports nothing and does not bump the modseq.
- `UID FETCH ... (CHANGEDSINCE n VANISHED)` reports expunges this session has not yet been
  told about as a plain `* VANISHED`, then everything expunged since `n` as
  `VANISHED (EARLIER)` - **including on a repeat of the same request**. Dovecot does not
  remember what a session was told. (The first harness assumed the opposite.)
- The tagged OK of a command whose response carried a plain VANISHED carries
  `[HIGHESTMODSEQ n]`; after IDLE it is sent untagged.
- Several appends are reported as one `EXISTS`; `RECENT` is per session and only sent when
  the session's count changed; `EXAMINE` reports but does not claim `\Recent`.
- `CREATE a/b` creates `a` as `\Noselect`; deleting the last child deletes it again.
- IDLE notifications for a single change arrive ~0.5 s later; a burst of changes within a
  few milliseconds is reported only partly during IDLE and the rest after `DONE`. With
  maildir the default `mailbox_idle_check_interval` (30 s) is not what causes this.
- **A session's FETCH is answered from its own view.** If another session expunged messages
  since this session last synced, `UID FETCH 1:*` still returns those messages, and the
  `EXPUNGE` / `VANISHED` lines come *after* the FETCH data. `STATUS` on the selected mailbox
  is likewise stale (Dovecot marks it `[CLIENTBUG]`). mailsync's background connection keeps
  the last folder it touched selected, so a deep scan right after a deletion sees the old
  message set and only converges on the following pass (`plain-expunge-found-by-deep-scan`
  records this as an xfail). The fake models this (`probe_stale_fetch`).

## Server kinds

- `fake:<personality>` - `dovecot` (baseline), `plain` (no CONDSTORE/QRESYNC: the deep-scan
  branch), `proton-bridge`, `gateway-duplicate-list`, `netease`, `courier`, `outlook`,
  `icloud`, `gmail`, `yahoo` (permuted COPYUID). `fake:dovecot-without-condstore-qresync` style names strip capabilities.
  Hostname-gated engine behaviour (iCloud, NetEase, Outlook) needs the account's
  `imap_host` to resolve to 127.0.0.1; a scenario declares `{fake: netease, imap_host:
  imap.163.com}` and is skipped with instructions unless `/etc/hosts` maps it.
- `dovecot:<profile>` - `qresync`, `plain` (capability override, as the #140 control run),
  `proton-like` (plain + `\All` "All Mail"), `tls` (self-signed, implicit TLS), `sdbox`.

## Scenarios

| scenario | protects | servers |
|---|---|---|
| baseline-initial-sync | ingestion, flags, threads, attachments, no churn | all |
| condstore-initial-sync-large | #140 chunked walk on QRESYNC (2500 msgs) | fake, dovecot |
| sparse-uid-space | #140 review: UIDNEXT 20001 with 1100 msgs; 4500 over a hole | fake, dovecot |
| qresync-bulk-expunge-during-idle | #141 wide VANISHED during IDLE | fake, dovecot |
| qresync-wide-vanished-range | #141 bounded-range query, ghost at range end | fake, dovecot |
| qresync-two-expunges-during-idle | #141 second VANISHED line | fake, dovecot |
| qresync-vanished-during-body-fetch | #141 VANISHED on a non-IDLE command | fake |
| plain-expunge-found-by-deep-scan | deep-scan deletion detection | fake, dovecot |
| remote-move-to-archive | move followed once, no unpersist | fake, dovecot |
| remote-flag-changes | flags via IDLE and CHANGEDSINCE | fake, dovecot |
| new-mail-during-idle | EXISTS during IDLE, folders nobody idles on | fake, dovecot |
| self-addressed-inbox-and-sent | placements: same bytes in INBOX and Sent | fake, dovecot |
| o365-duplicate-sent-copies | placements: identical copies at adjacent UIDs | fake, dovecot |
| proton-all-mail-duplicates | #137 `\All` skip | fake, dovecot |
| gateway-duplicate-list-entries | #139 duplicate LIST lines | fake |
| netease-id-before-select | #121 ID before SELECT, STATUS without UIDNEXT | fake (hosts alias) |
| uidvalidity-change | UIDVALIDITY remap | fake, dovecot |
| two-folders-identical-messages | #140 non-converging gap scan must settle | fake, dovecot |
| client-task-move | ChangeFolder/Starred/Unread tasks | fake, dovecot |
| client-task-trash-and-expunge | Trash + ExpungeAllInFolder | fake, dovecot |
| connection-dropped-during-idle | reconnect after the server drops connections | fake, dovecot |
| send-draft | SendDraftTask over SMTP, Sent copy, self-addressed delivery | fake (+smtp) |
| gmail-labels | All Mail + X-GM-LABELS views, webmail archive/label/star, trash task | fake |
| remote-move-destination-scanned-first | placements: move seen "present in B" before "gone from A"; never deleted/re-created | fake ×2, dovecot |
| trash-message-with-two-placements | placements: trash from Inbox takes the Sent copy too; a plain archive does not | fake ×2, dovecot |
| flag-change-on-second-placement | placements: per-placement flags on the Sent copy of a self-addressed message | fake ×2, dovecot |
| gmail-send-and-labels | Gmail: no Sent APPEND on send, one All Mail placement; ChangeLabelsTask keeps one placement | fake |
| migration-from-pre-placements-db | V10 migration of an 0df7864 database + a DB caught mid-sweep | fake, dovecot |
| heavy-fetch-truncation-backlog | #140 1024-header truncation and draining-backlog back-off | fake, dovecot |
| modseq-truncation | CHANGEDSINCE gap > MODSEQ_TRUNCATION_THRESHOLD bounds the request to the newest UIDs | fake, dovecot |
| mid-pass-foreground-tombstone | foreground VANISHED while the background's stale FETCH of INBOX is in flight; no resurrection | fake ×2 |
| trash-two-placements-without-uidplus | trash of INBOX+Sent copies without COPYUID (dest-fetch fallback) | fake |
| undo-move-restores-placements | sourceFolderIds move of one copy, and trash of both copies, each undone via restorePlacements | fake ×2, dovecot ×2 |
| undo-before-remote-phase | undo queued while the move's MOVE is held: the undo's marker survives the move's commit | fake ×2, dovecot |
| move-into-folder-holding-a-copy | move into a folder that already holds a copy leaves two there; undo returns the added one | fake ×2, dovecot |
| mark-read-fans-out-to-all-placements | ChangeUnreadTask by threadIds hits every placement | fake ×2, dovecot |
| uidvalidity-change-large-mailbox | #140 truncated UIDVALIDITY rebuild re-loops (2 500 msgs) | fake ×2 |
| synced-draft-destroy (+ -courier) | DestroyDraftTask on a server-synced draft and a local UID-0 draft | fake ×2, dovecot |
| remote-move-while-task-in-flight | another client moves a message's only copy while a star task holds its syncedAt lock; the new copy is recorded, no unpersist | fake ×2, dovecot |
| move-rejected-by-server | MOVE answered NO [OVERQUOTA]: the copy shows in INBOX again and the syncedAt lock is released | fake ×2 |
| orphan-sweep-with-unreadable-folder | a folder whose STATUS always fails delays the orphan sweep by ORPHAN_SWEEP_MAX_WAIT instead of disabling it | fake |
| orphan-sweep-waits-for-initial-walk | an initial walk that moves down every pass holds the orphan sweep past ORPHAN_SWEEP_MAX_WAIT | fake |

Known engine failures are marked `xfail` in the scenario with the reason; `pytest -rxX`
lists them and an `XPASS` line means the marker can be removed. Each open one gets a write-up
in `docs/tasks/` (symptom, failing scenario, how it was found, likely cause, definition of
done) that is deleted when the fix lands. As of 2026-09-22 there are none: the suite found
five engine bugs on 2026-09-19/20 (two VANISHED-accumulator segfaults, the `\\All` mailbox
never clearing `busy`, and two consequences of Dovecot's stale per-connection view) and all
five were fixed within two days (`6c1395e`, `4c25380`, `c5619a8`, `5080fb2`). The invariants
found a sixth on 2026-09-22, both workers applying one server change's thread delta to a
message loaded outside the transaction, fixed the same day.

Scenario timing rule: after a server-side change on a QRESYNC server, wait for the engine to
receive it (`wait: {log: "recv \\* VANISHED"}`) before forcing a pass; Dovecot delivers
IDLE notifications ~0.5 s after the change and a `wake-workers` that interrupts IDLE first
defers the notification to the next cycle.

## Stopgaps and next steps

- **Scan intervals.** `DEEP_SCAN_INTERVAL` (10 min), `SHALLOW_SCAN_INTERVAL`, the CONDSTORE gap
  scan (24 h) and the 120 s worker sleep are compile-time constants. `sync: pass` covers the
  sleep via `wake-workers`; `force_scans` covers the rest by zeroing `lastDeep`/`lastShallow`
  in `Folder.localStatus` (a write to a database the engine owns - safe in practice because
  `syncNow` re-reads folders each pass, but a hack). Environment-variable overrides in the
  engine will replace it.
- **Gmail.** `fakeimap.store.GmailStore` models INBOX / Sent Mail / Starred / Important /
  Drafts / user labels as views of `[Gmail]/All Mail` with per-view UIDs, label changes as
  EXISTS/EXPUNGE in the affected views, deletion-from-a-view as label removal, deletion from
  All Mail as a move to Trash, and `X-GM-LABELS` excluding the selected view's own label.
  Labels are sent as quoted astrings (`"\\Inbox"`), which is what libetpan parses; the
  exact quoting Gmail uses is NEEDS-RECORDING. Unsolicited FETCH responses include
  `X-GM-LABELS`, which real Gmail may not do (also NEEDS-RECORDING).
- **SMTP.** `fakeimap/smtp.py` (AUTH PLAIN/LOGIN, optional Postfix-style HELO rejection,
  delivery of self-addressed mail back into INBOX) is enabled per server spec with
  `{fake: dovecot, smtp: true}`; `--mode test` and the EHLO fallback (#135) have no scenario yet.
  `{fake: gmail, smtp: true, smtp_sent_copy: "[Gmail]/Sent Mail"}` also files every submitted
  message under the named mailbox, as Gmail's submission service saves sent mail under `\Sent`
  by itself - the copy the engine's send path looks for before it APPENDs its own
  (`gmail-send-and-labels`).
- **Recording real providers.** `tools/record_personality.py HOST PORT USER --name NAME`
  captures greeting, capabilities, NAMESPACE, ID, LIST, STATUS/SELECT and FETCH shapes from a
  real account (no message content) into `fakeimap/recordings/`; use it to replace every
  `NEEDS-RECORDING` personality field with the real text.
- **Live accounts.** `Server.truth()` works against any IMAP server, so a pre-merge smoke
  against real accounts (run N passes, assert `stable`) needs only credentials plumbing.
