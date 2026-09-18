# QRESYNC `VANISHED` reproduction harness

A scriptable IMAP4rev1 + CONDSTORE + QRESYNC + IDLE server (`imapd.py`) that a real
`mailsync` process can sync against, used to reproduce - and to verify the fix for - the
class of bug where a server-side expunge is mishandled and the local database permanently
diverges from the server.

These bugs only bite on servers that advertise both CONDSTORE and QRESYNC (FastMail,
Dovecot, Zoho, ...). On those servers `SyncWorker::syncNow` takes the
`syncFolderChangesViaCondstore` branch exclusively - there is no periodic deep or shallow
scan to fall back on - so anything missed when `HIGHESTMODSEQ` advances is missed forever.

## Running

```bash
# build mailsync first (see BUILDING.md), then:
cd test/qresync-vanished
./run.sh scenario_bulk_archive.json mytest 45

# artifacts land in runs/mytest/
#   imapd.log   - the full IMAP conversation, both connections
#   deltas.log  - mailsync's stdout (delta stream + logging)
#   config/edgehill.db - the resulting database
sqlite3 runs/mytest/config/edgehill.db "select count(*) from Message;"
```

Set `MAILSYNC_BIN` to point at a different binary (handy for before/after comparisons).

## Scenarios

| Scenario | What the server does | What used to go wrong |
| --- | --- | --- |
| `scenario_bulk_archive.json` | Expunges UIDs 140-195 (56 messages) during IDLE and reports `* VANISHED 140:195` | 178 of 200 messages were deleted locally - 123 of them still existed on the server |
| `scenario_wide_range.json` | Expunges UIDs 20-120 and reports `* VANISHED 20:120` | Everything below UID 120 was deleted, and UID 120 itself was left behind as a ghost |
| `scenario_body_fetch.json` | Expunges UID 150 and reports `* VANISHED 150` in the middle of a `UID FETCH ... BODY.PEEK[]` | The notification was dropped; `HIGHESTMODSEQ` then advanced past it, so UID 150 stayed forever |
| `scenario_two_vanished_lines.json` | Sends two separate `* VANISHED` lines during one IDLE | Only the first line was processed |
| `scenario_control.json` | Never expunges anything | Control - nothing should ever be unlinked |

`suppress_after_report` (default `true`) models the behaviour real QRESYNC servers have:
once a connection has been told a UID is gone, a later `FETCH ... (CHANGEDSINCE n VANISHED)`
on that same connection does not repeat it.

## Scenario format

```jsonc
{
  "idle_seconds": 3,             // how long the server waits before scripted IDLE events
  "suppress_after_report": true,
  "mailboxes": [
    {"name": "INBOX", "attrs": ["\\HasNoChildren"], "messages": 200}
  ],
  "events": [
    {
      "when": "during_idle",     // during_idle | before_body_fetch | before_fetch | after_fetch
      "do": "expunge",
      "mailbox": "INBOX",
      "uids": [150],
      "announce_here": true,     // emit an untagged VANISHED on this connection
      "delay": 14,               // seconds after server start before the event may fire
      "once": true
    }
  ]
}
```

The server is deliberately minimal: it implements only the commands mailsync actually
issues, and it is not a general-purpose IMAP server.
