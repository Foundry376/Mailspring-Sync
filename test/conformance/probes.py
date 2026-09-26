"""
Probes: the IMAP exchanges mailsync performs, run identically against the fake server and
against Dovecot, with the responses normalized so that only substantive differences
remain. Each probe returns {label: [normalized lines]}. A difference that turns out to be
legitimate implementation freedom is recorded in ALLOWED_DIFFERENCES with the reason; a
difference that is not is a bug in the fake.
"""
import re

from .rawimap import RawImap

# Normalizations: values that legitimately differ between any two servers.
_SUBS = [
    (rb"UIDVALIDITY \d+", b"UIDVALIDITY <n>"),
    (rb"HIGHESTMODSEQ \d+", b"HIGHESTMODSEQ <n>"),
    (rb"MODSEQ \(\d+\)", b"MODSEQ (<n>)"),
    (rb"COPYUID \d+ ", b"COPYUID <n> "),
    (rb"APPENDUID \d+ ", b"APPENDUID <n> "),
    (rb"INTERNALDATE \"[^\"]+\"", b"INTERNALDATE \"<date>\""),
    (rb"\\Recent ?", b""),
    (rb"\( ", b"("),
    (rb" \)", b")"),
    (rb"\s*\(\d+\.\d+ \+ \d+\.\d+ secs\)\.?", b""),
]


def normalize(line: bytes) -> bytes:
    for rx, rep in _SUBS:
        line = re.sub(rx, rep, line)
    # tagged responses: keep status and response code, drop the free text
    m = re.match(rb"^a\d+ (OK|NO|BAD)(?: (\[[^\]]*\]))?.*$", line)
    if m:
        return b"<tag> " + m.group(1) + (b" " + m.group(2) if m.group(2) else b"")
    # untagged OK/NO/BAD with a response code: keep the code only
    m = re.match(rb"^\* (OK|NO|BAD) (\[[^\]]*\]).*$", line)
    if m:
        return b"* " + m.group(1) + b" " + m.group(2)
    m = re.match(rb"^\* (OK|NO|BAD|BYE) .*$", line)
    if m:
        return b"* " + m.group(1) + b" <text>"
    return line.rstrip()


def capabilities_in(lines) -> set:
    for l in lines:
        m = re.search(rb"CAPABILITY ([^\]]+)", l)
        if m:
            return set(m.group(1).split())
    return set()


class Rig:
    """Two raw connections to one server, plus the Server object for out-of-band changes."""

    def __init__(self, server, user="test", password="pass", ssl=False):
        self.server = server
        self.a = RawImap(server.host, server.port, ssl=ssl)
        self.b = RawImap(server.host, server.port, ssl=ssl)
        for c in (self.a, self.b):
            c.cmd(f"LOGIN {user} {password}")

    def close(self):
        self.a.close()
        self.b.close()


def probe_session_setup(rig: Rig):
    c = RawImap(rig.server.host, rig.server.port)
    out = {"greeting": [normalize(c.greeting.rstrip(b"\r\n"))]}
    out["greeting_caps"] = [b" ".join(sorted(capabilities_in([c.greeting])))]
    login = c.cmd("LOGIN test pass")
    out["login"] = [normalize(l) for l in login]
    out["login_caps"] = [b" ".join(sorted(capabilities_in(login)))]
    out["enable"] = [normalize(l) for l in c.cmd("ENABLE QRESYNC")]
    out["namespace"] = [normalize(l) for l in c.cmd("NAMESPACE")]
    out["id"] = [normalize(l)[:6] for l in c.cmd('ID ("name" "Mailspring")')]
    c.close()
    return out


def probe_list_status(rig: Rig):
    out = {}
    out["list"] = sorted(normalize(l) for l in rig.a.cmd('LIST "" "*"'))
    out["list_inbox"] = [normalize(l) for l in rig.a.cmd('LIST "" "INBOX"')]
    out["lsub"] = sorted(normalize(l) for l in rig.a.cmd('LSUB "" "*"'))
    for mb in ("INBOX", "Archive"):
        out[f"status_{mb}"] = [normalize(l) for l in rig.a.cmd(f"STATUS {mb} (UNSEEN MESSAGES RECENT UIDNEXT UIDVALIDITY HIGHESTMODSEQ)")]
    out["status_missing"] = [normalize(l) for l in rig.a.cmd("STATUS Nope (MESSAGES)")]
    return out


def probe_select_fetch(rig: Rig):
    out = {}
    rig.a.cmd("ENABLE QRESYNC")
    out["select"] = [normalize(l) for l in rig.a.cmd("SELECT INBOX")]
    out["examine"] = [normalize(l) for l in rig.a.cmd("EXAMINE Archive")]
    rig.a.cmd("SELECT INBOX")
    out["headers"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1:3 (UID FLAGS INTERNALDATE ENVELOPE BODY.PEEK[HEADER.FIELDS (References)])")]
    out["attrs"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1:* (UID FLAGS)")]
    out["modseq"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1:2 (UID FLAGS MODSEQ)")]
    out["body"] = [normalize(l) for l in rig.a.cmd("UID FETCH 2 BODY.PEEK[]")]
    out["bodystructure"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1:2 (BODYSTRUCTURE)")]
    out["rfc822size"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1 (RFC822.SIZE)")]
    out["header_section"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1 (BODY.PEEK[HEADER])")]
    out["part_section"] = [normalize(l) for l in rig.a.cmd("UID FETCH 3 (BODY.PEEK[1] BODY.PEEK[1.MIME])")]
    out["partial"] = [normalize(l) for l in rig.a.cmd("UID FETCH 1 (BODY.PEEK[]<0.40>)")]
    out["nonexistent_uid"] = [normalize(l) for l in rig.a.cmd("UID FETCH 9999 (UID FLAGS)")]
    out["search_header"] = [normalize(l) for l in rig.a.cmd('UID SEARCH HEADER Message-ID "harness-2-"')]
    out["search_all"] = [normalize(l) for l in rig.a.cmd("UID SEARCH ALL")]
    out["search_unseen"] = [normalize(l) for l in rig.a.cmd("UID SEARCH UNSEEN")]
    return out


def probe_store(rig: Rig):
    out = {}
    rig.a.cmd("ENABLE QRESYNC")
    rig.a.cmd("SELECT INBOX")
    rig.b.cmd("ENABLE QRESYNC")
    rig.b.cmd("SELECT INBOX")
    out["store_add"] = [normalize(l) for l in rig.a.cmd("UID STORE 1 +FLAGS (\\Flagged)")]
    out["store_silent"] = [normalize(l) for l in rig.a.cmd("UID STORE 2 +FLAGS.SILENT (\\Flagged)")]
    out["store_remove"] = [normalize(l) for l in rig.a.cmd("UID STORE 1 -FLAGS (\\Flagged)")]
    out["store_noop_same_flag"] = [normalize(l) for l in rig.a.cmd("UID STORE 2 +FLAGS (\\Flagged)")]
    # what the *other* session learns at its next command
    out["other_session_noop"] = [normalize(l) for l in rig.b.cmd("NOOP")]
    return out


def probe_bulk_store_modseq(rig: Rig):
    """One STORE over many messages is one transaction: HIGHESTMODSEQ advances by one and every
    changed message reports that same value. Modseq *values* are normalized everywhere else, so
    this probe reports the advance and the number of distinct values instead. The engine's
    MODSEQ_TRUNCATION_THRESHOLD (SyncWorker.cpp) compares modseq differences, which makes the
    allocation rule observable behaviour."""
    out = {}
    rig.a.cmd("ENABLE CONDSTORE")
    sel = rig.a.cmd("SELECT INBOX")
    hms = int(re.search(rb"HIGHESTMODSEQ (\d+)", b"\n".join(sel)).group(1))
    lines = rig.a.cmd("UID STORE 1:6 +FLAGS (\\Flagged)")
    values = {int(m.group(1)) for l in lines for m in re.finditer(rb"MODSEQ \((\d+)\)", l)}
    hms2 = hms
    for l in rig.a.cmd("STATUS INBOX (HIGHESTMODSEQ)"):
        m = re.search(rb"HIGHESTMODSEQ (\d+)", l)
        if m:
            hms2 = int(m.group(1))
    out["bulk_store"] = [normalize(l) for l in lines]
    out["bulk_store_modseq_advance"] = [f"advance {hms2 - hms}".encode(), f"distinct {len(values)}".encode(),
                                        b"all-at-highest " + (b"yes" if values == {hms2} else b"no")]
    return out


def probe_changedsince(rig: Rig):
    out = {}
    rig.a.cmd("ENABLE QRESYNC")
    sel = rig.a.cmd("SELECT INBOX")
    hms = int(re.search(rb"HIGHESTMODSEQ (\d+)", b"\n".join(sel)).group(1))
    rig.b.cmd("SELECT INBOX")
    rig.b.cmd("UID STORE 3 +FLAGS.SILENT (\\Seen)")
    out["changedsince_flags"] = [normalize(l) for l in rig.a.cmd(f"UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE {hms})")]
    sel = rig.a.cmd("NOOP")
    hms2 = hms
    for l in rig.a.cmd("STATUS INBOX (HIGHESTMODSEQ)"):
        m = re.search(rb"HIGHESTMODSEQ (\d+)", l)
        if m:
            hms2 = int(m.group(1))
    rig.b.cmd("UID STORE 4:5 +FLAGS.SILENT (\\Deleted)")
    rig.b.cmd("UID EXPUNGE 4:5")
    out["changedsince_vanished"] = [normalize(l) for l in rig.a.cmd(f"UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE {hms2} VANISHED)")]
    # asked again with the same modseq: does the server repeat the VANISHED it already told us?
    out["changedsince_vanished_repeat"] = [normalize(l) for l in rig.a.cmd(f"UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE {hms2} VANISHED)")]
    return out


def probe_copy_move_append(rig: Rig):
    out = {}
    rig.a.cmd("ENABLE QRESYNC")
    rig.a.cmd("SELECT INBOX")
    rig.b.cmd("SELECT Archive")
    out["copy"] = [normalize(l) for l in rig.a.cmd("UID COPY 1:2 Archive")]
    out["move"] = [normalize(l) for l in rig.a.cmd("UID MOVE 3 Archive")]
    out["other_session_after_copy_move"] = [normalize(l) for l in rig.b.cmd("NOOP")]
    raw = b"From: x@example.test\r\nTo: test@example.test\r\nSubject: appended\r\nMessage-ID: <appended-1@example.test>\r\nDate: Mon, 05 Jan 2026 09:00:00 +0000\r\n\r\nbody\r\n"
    out["append"] = [normalize(l) for l in rig.a.cmd(f"APPEND Archive (\\Seen) {{{len(raw)}}}", literal=raw)]
    out["append_literal_plus"] = [normalize(l) for l in rig.a.cmd(f"APPEND Archive (\\Seen) {{{len(raw)}+}}\r\n" + raw.decode())]
    out["other_session_after_append"] = [normalize(l) for l in rig.b.cmd("NOOP")]
    out["copy_missing_dest"] = [normalize(l) for l in rig.a.cmd("UID COPY 1 Nope")]
    out["expunge_deleted"] = [normalize(l) for l in rig.a.cmd("UID STORE 6 +FLAGS.SILENT (\\Deleted)") + rig.a.cmd("EXPUNGE")]
    out["create_rename_delete"] = [normalize(l) for l in rig.a.cmd("CREATE Temp/Sub") + rig.a.cmd('LIST "" "Temp*"') + rig.a.cmd("RENAME Temp Temp2") + rig.a.cmd("DELETE Temp2/Sub") + rig.a.cmd("DELETE Temp2")]
    out["create_existing"] = [normalize(l) for l in rig.a.cmd("CREATE INBOX")]
    return out


def probe_idle(rig: Rig):
    """Notifications delivered to an idling session. Dovecot reports each change ~0.5s after it
    happens, but a burst of changes inside a few milliseconds is only partly reported during
    IDLE with the rest following DONE, so the operations are spaced out and the during-IDLE
    and at-DONE lines are compared as one set. RECENT and the untagged [HIGHESTMODSEQ] are
    bookkeeping whose timing varies and are dropped."""
    import time
    out = {}
    rig.a.cmd("ENABLE QRESYNC")
    rig.a.cmd("SELECT INBOX")
    rig.b.cmd("SELECT INBOX")
    raw = b"From: x@example.test\r\nTo: test@example.test\r\nSubject: during idle\r\nMessage-ID: <idle-1@example.test>\r\nDate: Mon, 05 Jan 2026 09:00:00 +0000\r\n\r\nbody\r\n"

    def spaced(*ops):
        for op in ops:
            op()
            time.sleep(1.2)

    rig.a.idle_start()
    spaced(lambda: rig.b.cmd(f"APPEND INBOX (\\Seen) {{{len(raw)}}}", literal=raw),
           lambda: rig.b.cmd("UID STORE 1 +FLAGS.SILENT (\\Flagged)"),
           lambda: rig.b.cmd("UID STORE 7:8 +FLAGS.SILENT (\\Deleted)"),
           lambda: rig.b.cmd("UID EXPUNGE 7:8"))
    during = rig.a.idle_collect(2.0)
    done = rig.a.idle_done()
    out["idle_qresync"] = _idle_lines(during + done)
    # a session that has NOT enabled QRESYNC sees sequence-number EXPUNGEs instead
    c = RawImap(rig.server.host, rig.server.port)
    c.cmd("LOGIN test pass")
    c.cmd("SELECT INBOX")
    c.idle_start()
    spaced(lambda: rig.b.cmd("UID STORE 9 +FLAGS.SILENT (\\Deleted)"), lambda: rig.b.cmd("UID EXPUNGE 9"))
    during = c.idle_collect(2.0)
    done = c.idle_done()
    out["idle_plain"] = _idle_lines(during + done)
    c.close()
    return out


def _idle_lines(lines) -> list:
    keep = []
    for l in lines:
        n = normalize(l)
        if n.startswith(b"<tag>") or b" RECENT" in n or b"[HIGHESTMODSEQ" in n:
            continue
        keep.append(n)
    return sorted(keep)


def probe_stale_fetch(rig: Rig):
    """A session with INBOX selected fetches after another session expunged: what does the
    FETCH return, and where do the EXPUNGE/VANISHED lines go?"""
    out = {}
    for label, enable in (("plain", None), ("qresync", "ENABLE QRESYNC")):
        c = RawImap(rig.server.host, rig.server.port)
        c.cmd("LOGIN test pass")
        if enable:
            c.cmd(enable)
        c.cmd("SELECT INBOX")
        base = 2 if label == "plain" else 8
        rig.b.cmd("SELECT INBOX")
        rig.b.cmd(f"UID STORE {base}:{base + 1} +FLAGS.SILENT (\\Deleted)")
        rig.b.cmd(f"UID EXPUNGE {base}:{base + 1}")
        out[f"stale_status_{label}"] = [normalize(l) for l in c.cmd("STATUS INBOX (MESSAGES)")]
        out[f"stale_fetch_{label}"] = [normalize(l) for l in c.cmd("UID FETCH 1:* (UID FLAGS)")]
        out[f"stale_fetch_again_{label}"] = [normalize(l) for l in c.cmd("UID FETCH 1:* (UID FLAGS)")]
        c.close()
    return out


PROBES = [probe_session_setup, probe_list_status, probe_select_fetch, probe_store, probe_bulk_store_modseq, probe_changedsince,
          probe_copy_move_append, probe_idle, probe_stale_fetch]
