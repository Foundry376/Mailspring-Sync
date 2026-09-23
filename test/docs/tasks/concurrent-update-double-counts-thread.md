# Task: two workers updating one message at once apply its thread delta twice

Status: open. Found 2026-09-22 by the harness's scenario-end invariants
(`harness/invariants.py`, check `thread_refcounts`) on the placements branch. Timing-dependent:
it shows on a different subset of messages, and sometimes on none, from run to run.

## Symptom

A single-message thread ends with `unread = 2` (or `starred = 2`, or a folder `_refs = 2`)
while its one message is unread once and has one copy in that folder. The thread list
badge and `ThreadCategory` / `ThreadCounts` are built from these counters, so they stay
wrong until the thread is rebuilt. Both workers log the same change in the same millisecond:

```
[background] - Updating message 2BTGaiYffHDZ... in INBOX
[background] -- Unread (false to true)
[foreground] - Updating message 2BTGaiYffHDZ... in INBOX
[foreground] -- Unread (false to true)
[foreground] -- Folders now {"k6XcFa...":1}
```

and the invariant reports:

```
[thread_refcounts] thread t:2BTGaiYffHDZ... 'Message 5' (1 messages): unread/starred = 2/0,
  expected 1/0 from messages [2BTGaiYffHDZ u=True folders={"k6XcFa...": 1}]
```

The same race on a new copy (`has a copy in INBOX (UID 22)` logged by both workers) leaves
the folder refcount at 2.

## Failing tests

Seen on 2026-09-22, each opting out with `invariants: {skip: [thread_refcounts]}`:
`remote-flag-changes` (fake:dovecot, fake:plain), `modseq-truncation` (fake:dovecot, 190
threads with `starred = 2`), `flag-change-on-second-placement` (fake:plain),
`remote-move-destination-scanned-first` (fake:plain). Any scenario where the idling
foreground connection and a background scan both see the same server change can hit it.

```bash
python3 test/run.py test/scenarios/remote-flag-changes.yaml --server fake:plain --keep
```

## Root cause

`MailProcessor::insertFallbackToUpdateMessage` loads the `Message` (MailProcessor.cpp,
`store->find<Message>(q)`) and `updateMessage` reads its placements before opening the
`updateMessage` transaction. The foreground (unsolicited FETCH after IDLE) and background
(CHANGEDSINCE / range scan) workers each load the message in its old state; each writes the
placement and saves the message, and `Message::afterSave` applies
`_lastSnapshot -> current` to the freshly loaded thread. The second save's `_lastSnapshot` is
the pre-change state the first save already accounted for, so the thread gets the +1 twice.
The message and placement rows end up correct; only the thread's incremental counters drift.

## Fix options

- Re-load the message (and its placements) inside the transaction before diffing, so the
  snapshot is the committed state; the write lock serialises the two workers.
- Or, in `Message::afterSave`, take the "before" snapshot from the row as committed at the
  start of the transaction rather than from the instance's load time.

## Definition of done

The four scenarios above drop their `skip: [thread_refcounts]` and pass repeatedly on every
server (`for i in 1 2 3 4 5; do python3 -m pytest test -k "remote-flag-changes or
modseq-truncation" --servers fake; done`).
