#!/usr/bin/env python3
"""
A small, scriptable IMAP4rev1 + CONDSTORE + QRESYNC + IDLE server used to
reproduce VANISHED-handling bugs in mailsync.

It is deliberately minimal but protocol-correct for the subset of commands
mailsync issues. Scenarios are driven by a JSON "script" file.
"""
import socket, socketserver, threading, sys, os, time, json, re, email.utils

LOG = open(os.environ.get("IMAPD_LOG", "/tmp/imapd.log"), "a", buffering=1)
LOCK = threading.RLock()


def log(*a):
    with LOCK:
        LOG.write(" ".join(str(x) for x in a) + "\n")


CAPS = ("IMAP4rev1 IDLE CONDSTORE QRESYNC ENABLE UIDPLUS MOVE "
        "LITERAL+ CHILDREN NAMESPACE AUTH=PLAIN")


BASE_TS = time.time() - 3600


def mkmsg(uid, subj):
    date = email.utils.formatdate(BASE_TS + uid)
    return (f"Message-ID: <msg{uid}@example.test>\r\n"
            f"From: Sender <sender@example.test>\r\n"
            f"To: Me <me@example.test>\r\n"
            f"Subject: {subj}\r\n"
            f"Date: {date}\r\n"
            f"MIME-Version: 1.0\r\n"
            f"Content-Type: text/plain; charset=utf-8\r\n"
            f"\r\n"
            f"Body of message {uid}.\r\n")


class Msg:
    def __init__(self, uid, modseq):
        self.uid = uid
        self.flags = ["\\Seen"]
        self.modseq = modseq
        self.raw = mkmsg(uid, f"Test message {uid}")
        self.subject = f"Test message {uid}"
        self.msgid = f"<msg{uid}@example.test>"
        self.date = email.utils.formatdate(BASE_TS + uid)

    def envelope(self):
        addr = '(("Sender" NIL "sender" "example.test"))'
        me = '(("Me" NIL "me" "example.test"))'
        return ("ENVELOPE (" + f'"{self.date}" "{self.subject}" ' +
                f"{addr} {addr} {addr} {me} NIL NIL NIL " +
                f'"{self.msgid}")')


class Mailbox:
    def __init__(self, name, attrs):
        self.name = name
        self.attrs = attrs
        self.uidvalidity = 4242
        self.uidnext = 1
        self.hms = 1
        self.msgs = []           # list of Msg, ordered by uid
        # UIDs the server has expunged but not yet told anybody about,
        # tracked per connection id -> set of uids pending report
        self.expunged = []       # [(uid, modseq)]

    def add(self, count):
        for _ in range(count):
            self.hms += 1
            m = Msg(self.uidnext, self.hms)
            self.uidnext += 1
            self.msgs.append(m)

    def expunge_uids(self, uids):
        self.hms += 1
        for m in list(self.msgs):
            if m.uid in uids:
                self.msgs.remove(m)
                self.expunged.append((m.uid, self.hms))

    def by_uid(self, uid):
        for m in self.msgs:
            if m.uid == uid:
                return m
        return None


class State:
    def __init__(self, script):
        self.script = script
        self.boxes = {}
        for spec in script["mailboxes"]:
            b = Mailbox(spec["name"], spec.get("attrs", []))
            b.add(spec.get("messages", 0))
            self.boxes[spec["name"]] = b
        self.events = list(script.get("events", []))
        self.fired = []
        # uids the server has already announced as VANISHED on a given
        # connection -- QRESYNC servers do not re-report these.
        self.suppress_after_report = script.get("suppress_after_report", True)
        self.reported = {}  # connid -> set(uid)
        self.done = threading.Event()
        self.started = time.time()

    def note(self, what):
        log("EVENT", what)
        self.fired.append(what)


STATE = None


def parse_set(s, uidnext, msgs):
    """Return a predicate-ish list of (lo, hi) with hi possibly None for '*'."""
    out = []
    for part in s.split(","):
        if ":" in part:
            a, b = part.split(":", 1)
        else:
            a = b = part
        lo = uidnext - 1 if a == "*" else int(a)
        hi = None if b == "*" else int(b)
        if hi is not None and hi < lo:
            lo, hi = hi, lo
        out.append((lo, hi))
    return out


def in_set(uid, ranges):
    for lo, hi in ranges:
        if hi is None:
            if uid >= lo:
                return True
        elif lo <= uid <= hi:
            return True
    return False


def fmt_set(uids):
    """Compress a sorted list of uids into an IMAP sequence set."""
    uids = sorted(uids)
    parts, i = [], 0
    while i < len(uids):
        j = i
        while j + 1 < len(uids) and uids[j + 1] == uids[j] + 1:
            j += 1
        parts.append(str(uids[i]) if i == j else f"{uids[i]}:{uids[j]}")
        i = j + 1
    return ",".join(parts)


class Conn(socketserver.StreamRequestHandler):
    connseq = 0

    def setup(self):
        super().setup()
        with LOCK:
            Conn.connseq += 1
            self.cid = Conn.connseq
        self.selected = None
        self.qresync = False
        self.condstore = False
        self.pending_vanished = []   # uids to emit as unsolicited VANISHED

    def send(self, line):
        log(f"S{self.cid}>", line)
        self.wfile.write((line + "\r\n").encode())

    def readline(self):
        line = self.rfile.readline()
        if not line:
            return None
        line = line.decode("utf-8", "replace").rstrip("\r\n")
        log(f"C{self.cid}<", line)
        return line

    # -- event scripting -------------------------------------------------
    def fire_events(self, hook):
        """Apply scripted events whose 'when' matches `hook`."""
        with LOCK:
            for ev in list(STATE.events):
                if ev["when"] != hook:
                    continue
                if time.time() - STATE.started < ev.get("delay", 0):
                    continue
                if ev.get("once", True):
                    STATE.events.remove(ev)
                box = STATE.boxes[ev["mailbox"]]
                if ev["do"] == "expunge":
                    box.expunge_uids(ev["uids"])
                    STATE.note(f"expunged {ev['uids']} from {box.name} at hook {hook}")
                    if ev.get("announce_here"):
                        # each event announces its own untagged VANISHED line
                        self.pending_vanished.append(list(ev["uids"]))
                elif ev["do"] == "stop":
                    STATE.note("stop")
                    STATE.done.set()

    def emit_pending_vanished(self):
        """Emit untagged VANISHED for expunges announced on this connection."""
        if not self.pending_vanished:
            return
        groups = self.pending_vanished
        self.pending_vanished = []
        for uids in groups:
            with LOCK:
                STATE.reported.setdefault(self.cid, set()).update(uids)
            self.send(f"* VANISHED {fmt_set(uids)}")

    # -- command handling ------------------------------------------------
    def handle(self):
        self.send(f"* OK [CAPABILITY {CAPS}] fake imapd ready")
        while True:
            line = self.readline()
            if line is None:
                return
            try:
                if not self.dispatch(line):
                    return
            except Exception as e:  # keep the server alive, surface in log
                log("ERROR", repr(e))
                import traceback
                log(traceback.format_exc())
                return

    def dispatch(self, line):
        m = re.match(r"^(\S+)\s+(\S+)\s*(.*)$", line)
        if not m:
            m2 = re.match(r"^(\S+)\s+(\S+)$", line)
            if not m2:
                self.send("* BAD unparsable")
                return True
        tag, cmd, rest = m.group(1), m.group(2).upper(), m.group(3)

        if cmd == "CAPABILITY":
            self.send(f"* CAPABILITY {CAPS}")
            self.send(f"{tag} OK CAPABILITY done")
        elif cmd == "LOGIN":
            self.send(f"{tag} OK [CAPABILITY {CAPS}] LOGIN done")
        elif cmd == "ID":
            self.send('* ID ("name" "fake")')
            self.send(f"{tag} OK ID done")
        elif cmd == "ENABLE":
            feats = rest.upper().split()
            en = []
            if "QRESYNC" in feats:
                self.qresync = True
                self.condstore = True
                en = ["QRESYNC"]
            elif "CONDSTORE" in feats:
                self.condstore = True
                en = ["CONDSTORE"]
            self.send(f"* ENABLED {' '.join(en)}")
            self.send(f"{tag} OK ENABLE done")
        elif cmd == "NAMESPACE":
            self.send('* NAMESPACE (("" "/")) NIL NIL')
            self.send(f"{tag} OK NAMESPACE done")
        elif cmd in ("LIST", "LSUB", "XLIST"):
            with LOCK:
                for b in STATE.boxes.values():
                    attrs = " ".join(b.attrs)
                    self.send(f'* {cmd} ({attrs}) "/" "{b.name}"')
            self.send(f"{tag} OK {cmd} done")
        elif cmd == "STATUS":
            self.do_status(tag, rest)
        elif cmd in ("SELECT", "EXAMINE"):
            self.do_select(tag, rest)
        elif cmd == "UID":
            self.do_uid(tag, rest)
        elif cmd == "NOOP":
            self.emit_pending_vanished()
            self.send(f"{tag} OK NOOP done")
        elif cmd == "IDLE":
            self.do_idle(tag)
        elif cmd == "CREATE":
            self.send(f"{tag} OK CREATE done")
        elif cmd == "SUBSCRIBE":
            self.send(f"{tag} OK SUBSCRIBE done")
        elif cmd == "CLOSE":
            self.send(f"{tag} OK CLOSE done")
        elif cmd == "LOGOUT":
            self.send("* BYE bye")
            self.send(f"{tag} OK LOGOUT done")
            return False
        else:
            self.send(f"{tag} BAD unsupported command {cmd}")
        return True

    def do_status(self, tag, rest):
        m = re.match(r'^"?([^"(]+?)"?\s+\((.*)\)\s*$', rest)
        name, items = m.group(1), m.group(2).upper().split()
        with LOCK:
            b = STATE.boxes.get(name)
            if b is None:
                self.send(f"{tag} NO no such mailbox")
                return
            vals = []
            for it in items:
                if it == "MESSAGES":
                    vals += ["MESSAGES", str(len(b.msgs))]
                elif it == "RECENT":
                    vals += ["RECENT", "0"]
                elif it == "UNSEEN":
                    vals += ["UNSEEN", "0"]
                elif it == "UIDNEXT":
                    vals += ["UIDNEXT", str(b.uidnext)]
                elif it == "UIDVALIDITY":
                    vals += ["UIDVALIDITY", str(b.uidvalidity)]
                elif it == "HIGHESTMODSEQ":
                    vals += ["HIGHESTMODSEQ", str(b.hms)]
            self.send(f'* STATUS "{name}" ({" ".join(vals)})')
        self.send(f"{tag} OK STATUS done")

    def do_select(self, tag, rest):
        name = rest.split(" ")[0].strip('"')
        with LOCK:
            b = STATE.boxes.get(name)
            if b is None:
                self.send(f"{tag} NO no such mailbox")
                return
            self.selected = name
            self.send(f"* {len(b.msgs)} EXISTS")
            self.send("* 0 RECENT")
            self.send(f"* OK [UIDVALIDITY {b.uidvalidity}] uidvalidity")
            self.send(f"* OK [UIDNEXT {b.uidnext}] uidnext")
            self.send("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)")
            self.send("* OK [PERMANENTFLAGS (\\Answered \\Flagged \\Deleted "
                      "\\Seen \\Draft \\*)] limited")
            if self.condstore or self.qresync:
                self.send(f"* OK [HIGHESTMODSEQ {b.hms}] highest")
        self.send(f"{tag} OK [READ-WRITE] SELECT done")

    def do_uid(self, tag, rest):
        m = re.match(r"^(\S+)\s+(.*)$", rest)
        sub, args = m.group(1).upper(), m.group(2)
        if sub == "FETCH":
            self.do_uid_fetch(tag, args)
        elif sub == "SEARCH":
            self.do_uid_search(tag, args)
        elif sub == "STORE":
            self.emit_pending_vanished()
            self.send(f"{tag} OK UID STORE done")
        elif sub in ("COPY", "MOVE"):
            self.emit_pending_vanished()
            self.send(f"{tag} OK UID {sub} done")
        else:
            self.send(f"{tag} BAD unsupported UID {sub}")

    def do_uid_search(self, tag, args):
        with LOCK:
            b = STATE.boxes[self.selected]
            self.send("* SEARCH " + " ".join(str(m.uid) for m in b.msgs))
        self.send(f"{tag} OK UID SEARCH done")

    def fetch_att(self, msg, items):
        """Build the FETCH attribute list for one message."""
        up = items.upper()
        out = []
        if "UID" in up:
            out.append(f"UID {msg.uid}")
        if "FLAGS" in up:
            out.append(f"FLAGS ({' '.join(msg.flags)})")
        if "MODSEQ" in up:
            out.append(f"MODSEQ ({msg.modseq})")
        if "INTERNALDATE" in up:
            out.append('INTERNALDATE "01-Jan-2024 00:00:00 +0000"')
        if "RFC822.SIZE" in up:
            out.append(f"RFC822.SIZE {len(msg.raw)}")
        if "ENVELOPE" in up:
            out.append(msg.envelope())
        literal = None
        mh = re.search(r"BODY(?:\.PEEK)?\[HEADER\.FIELDS \(([^)]*)\)\]", up)
        if mh:
            wanted = mh.group(1).split()
            lines = []
            for ln in msg.raw.split("\r\n\r\n")[0].split("\r\n"):
                if ":" in ln and ln.split(":")[0].upper() in wanted:
                    lines.append(ln)
            body = "\r\n".join(lines) + "\r\n\r\n"
            literal = (f"BODY[HEADER.FIELDS ({mh.group(1)})]", body)
        elif re.search(r"BODY(?:\.PEEK)?\[\]", up):
            literal = ("BODY[]", msg.raw)
        elif "RFC822" in up and "RFC822.SIZE" not in up:
            literal = ("RFC822", msg.raw)
        return out, literal

    def do_uid_fetch(self, tag, args):
        m = re.match(r"^(\S+)\s+(.*)$", args)
        seq, items = m.group(1), m.group(2)
        changedsince = None
        want_vanished = False
        cs = re.search(r"\(CHANGEDSINCE (\d+)(\s+VANISHED)?\)", items, re.I)
        if cs:
            changedsince = int(cs.group(1))
            want_vanished = bool(cs.group(2))
            items = items[:cs.start()]

        if re.search(r"BODY(?:\.PEEK)?\[\]", items, re.I):
            self.fire_events("before_body_fetch")
        self.fire_events("before_fetch")
        with LOCK:
            b = STATE.boxes[self.selected]
            ranges = parse_set(seq, b.uidnext, b.msgs)
            matched = [x for x in b.msgs if in_set(x.uid, ranges)]
            if changedsince is not None:
                matched = [x for x in matched if x.modseq > changedsince]
            msgs = list(matched)
            if want_vanished:
                already = STATE.reported.get(self.cid, set())
                gone = [u for (u, ms) in b.expunged if ms > changedsince]
                if STATE.suppress_after_report:
                    gone = [u for u in gone if u not in already]
                if gone:
                    STATE.reported.setdefault(self.cid, set()).update(gone)
                    self.send(f"* VANISHED (EARLIER) {fmt_set(gone)}")
                else:
                    log(f"S{self.cid}> (no VANISHED to report "
                        f"since modseq {changedsince})")

        # unsolicited VANISHED riding along on this command's response
        self.emit_pending_vanished()

        for msg in msgs:
            atts, literal = self.fetch_att(msg, items)
            if literal is None:
                self.send(f"* {self.seqno(msg)} FETCH ({' '.join(atts)})")
            else:
                name, data = literal
                head = " ".join(atts + [f"{name} {{{len(data)}}}"])
                self.send(f"* {self.seqno(msg)} FETCH ({head}")
                self.wfile.write(data.encode())
                self.wfile.write(b")\r\n")
                log(f"S{self.cid}>", f"<literal {len(data)} bytes>)")
        self.send(f"{tag} OK UID FETCH done")
        self.fire_events("after_fetch")

    def seqno(self, msg):
        b = STATE.boxes[self.selected]
        return b.msgs.index(msg) + 1

    def do_idle(self, tag):
        import select as _select
        self.send("+ idling")
        # Scripted events fire once their `delay` has elapsed; keep polling.
        while True:
            self.fire_events("during_idle")
            self.emit_pending_vanished()
            r, _, _ = _select.select([self.connection], [], [], 0.25)
            if not r:
                continue
            line = self.rfile.readline()
            if not line:
                return
            if line.decode("utf-8", "replace").strip().upper() == "DONE":
                log(f"C{self.cid}<", "DONE")
                self.emit_pending_vanished()
                self.send(f"{tag} OK IDLE terminated")
                return


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    global STATE
    script = json.load(open(sys.argv[1]))
    STATE = State(script)
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 11143
    srv = Server(("127.0.0.1", port), Conn)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    log(f"=== listening on {port}, script {sys.argv[1]} ===")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
