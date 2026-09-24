"""
The IMAP protocol layer of the fake server. One Session per TCP connection; state that
several connections share lives in Store.

Untagged notifications follow the real-server model: a Store change is queued on every
session that has the mailbox selected and is emitted at that session's next opportunity
(its next command, or immediately while it is in IDLE). A session is told about an
expunge exactly once, in sequence-number form or as VANISHED once QRESYNC is enabled.
"""
import base64
import re
import select
import socketserver
import ssl
import threading
import time
from datetime import datetime, timezone
from typing import Callable, Optional

from . import imapfmt
from .personalities import Personality, personality as get_personality
from .store import GmailStore, LabelView, Message, Store, StoreEvent

CRLF = b"\r\n"


class ProtocolError(Exception):
    pass


# -- tokenizer ------------------------------------------------------------------------------

class Tokenizer:
    """Splits an IMAP command line into atoms, quoted strings, literals and nested lists.
    Literals ({n} / {n+}) pull more bytes from the connection, sending the continuation
    request when the client did not use LITERAL+."""

    def __init__(self, session, line: bytes):
        self.s = session
        self.buf = line
        self.pos = 0

    def rest(self) -> bytes:
        return self.buf[self.pos:]

    def skip_ws(self):
        while self.pos < len(self.buf) and self.buf[self.pos:self.pos + 1] == b" ":
            self.pos += 1

    def eof(self) -> bool:
        self.skip_ws()
        return self.pos >= len(self.buf)

    def peek(self) -> bytes:
        self.skip_ws()
        return self.buf[self.pos:self.pos + 1]

    def token(self):
        self.skip_ws()
        if self.pos >= len(self.buf):
            return None
        c = self.buf[self.pos:self.pos + 1]
        if c == b"(":
            self.pos += 1
            items = []
            while True:
                self.skip_ws()
                if self.buf[self.pos:self.pos + 1] == b")":
                    self.pos += 1
                    return items
                items.append(self.token())
        if c == b'"':
            self.pos += 1
            out = bytearray()
            while self.pos < len(self.buf):
                ch = self.buf[self.pos:self.pos + 1]
                self.pos += 1
                if ch == b"\\":
                    out += self.buf[self.pos:self.pos + 1]
                    self.pos += 1
                elif ch == b'"':
                    break
                else:
                    out += ch
            return out.decode("utf-8", "surrogateescape")
        if c == b"{":
            end = self.buf.index(b"}", self.pos)
            spec = self.buf[self.pos + 1:end].decode()
            self.pos = end + 1
            plus = spec.endswith("+") or spec.endswith("-")
            n = int(spec.rstrip("+-"))
            if not plus:
                self.s.send_raw(b"+ OK" + CRLF)
            data = self.s.read_exact(n)
            # the remainder of the command follows the literal on a new line
            self.buf = self.buf[:self.pos] + self.s.read_line()
            return data
        # atom (may include [] for BODY[...] and <> for partials)
        start = self.pos
        depth = 0
        while self.pos < len(self.buf):
            ch = self.buf[self.pos:self.pos + 1]
            if ch == b"[":
                depth += 1
            elif ch == b"]":
                depth -= 1
            elif depth == 0 and ch in (b" ", b")", b"("):
                break
            self.pos += 1
        return self.buf[start:self.pos].decode("utf-8", "surrogateescape")


# -- session -------------------------------------------------------------------------------

class Session(socketserver.StreamRequestHandler):
    seq = 0
    rbufsize = 0   # unbuffered: select() on the socket then reflects exactly what is unread

    def setup(self):
        super().setup()
        srv: "FakeImapServer" = self.server.owner
        with srv.lock:
            Session.seq += 1
            self.sid = Session.seq
        self.srv = srv
        self.store: Store = srv.store
        self.p: Personality = srv.personality
        self.uidplus = "UIDPLUS" in self.p.capability_set(True)
        self.authenticated = False
        self.selected: Optional[str] = None
        self.view: list = []                 # seqno -> uid (index 0 = seqno 1)
        self.condstore = False
        self.qresync = False
        self.id_seen = False
        self.tls = False
        self.idling = False
        self.has_idled = False
        self.commands: list = []
        self.reject_next: Optional[tuple] = None  # (code, text): answer the next command NO, set by a hook
        self.pending: list = []              # StoreEvents not yet reported
        self.recent_uids: set = set()        # \Recent messages this session claimed
        self.sent_vanished = False           # a VANISHED went out during the current command
        self.cv = threading.Condition()
        self.closed = False
        self.store.add_listener(self._on_store_event)
        srv.sessions.append(self)
        srv.log(self.sid, "*", "connected")

    def finish(self):
        self.store.remove_listener(self._on_store_event)
        self.closed = True
        self.srv.log(self.sid, "*", "disconnected")
        super().finish()

    # -- io ---------------------------------------------------------------------------------

    def send_raw(self, data: bytes):
        try:
            self.wfile.write(data)
            self.wfile.flush()
        except (BrokenPipeError, OSError):
            self.closed = True

    def send(self, line: bytes):
        for l in line.split(CRLF):
            self.srv.log(self.sid, "S", l)
        self.send_raw(line + CRLF)

    def read_line(self) -> bytes:
        line = self.rfile.readline()
        if not line:
            raise EOFError
        return line.rstrip(b"\r\n")

    def read_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    # -- notifications -----------------------------------------------------------------------

    def _on_store_event(self, ev: StoreEvent):
        if self.selected is None or ev.mailbox != self.selected:
            return
        if ev.kind == "flags" and ev.origin is self:
            return  # our own STORE already reported the new flags
        with self.cv:
            self.pending.append(ev)
            self.cv.notify_all()

    def flush_events(self, allow_expunge: bool = True):
        """Emit queued untagged responses. EXPUNGE/VANISHED are withheld while a
        sequence-number FETCH/STORE/SEARCH is in progress (RFC 3501 7.4.1)."""
        with self.cv:
            events, self.pending = self.pending, []
        held = []
        mb = self.store.get(self.selected) if self.selected else None
        new_uids = []
        flag_uids = set()
        reported_gone = []
        for ev in events:
            if mb is None:
                continue
            if ev.kind == "exists":
                new_uids += [u for u in ev.uids if u not in self.view and u not in new_uids and mb.by_uid(u) is not None]
            elif ev.kind == "expunge":
                if not allow_expunge:
                    held.append(ev)
                    continue
                gone = [u for u in ev.uids if u in self.view]
                if not gone:
                    continue
                reported_gone += gone
                if self.qresync:
                    for u in gone:
                        self.view.remove(u)
                    self.send(b"* VANISHED " + _seqset(gone).encode())
                    self.sent_vanished = True
                else:
                    for u in sorted(gone, reverse=True):
                        seq = self.view.index(u) + 1
                        self.view.remove(u)
                        self.send(b"* %d EXPUNGE" % seq)
            elif ev.kind == "flags":
                flag_uids.update(u for u in ev.uids if u in self.view and mb.by_uid(u) is not None)
        for u in sorted(flag_uids, key=self.view.index):
            # one FETCH per changed message with its current flags, as Dovecot reports it
            self.send(b"* %d FETCH (" % (self.view.index(u) + 1) + self._flags_atts(mb.by_uid(u), include_uid=self.qresync) + b")")
        if new_uids:
            # One EXISTS for everything that arrived, then RECENT: the count of \Recent messages
            # this session has claimed, which is how Dovecot reports it.
            self.view += sorted(new_uids)
            claimed = [u for u in new_uids if u in mb.recent]
            mb.recent.difference_update(claimed)
            self.recent_uids.update(claimed)
            before = len(self.recent_uids & set(self.view))
            self.recent_uids &= set(self.view)
            self.send(b"* %d EXISTS" % len(self.view))
            if len(self.recent_uids) != before or claimed:
                self.send(b"* %d RECENT" % len(self.recent_uids))
        if held:
            with self.cv:
                self.pending = held + self.pending
        return reported_gone

    def _flags_atts(self, m: Message, include_uid: bool, flags: bool = True, uid: Optional[int] = None) -> bytes:
        # Dovecot's order for unsolicited and STORE responses: UID, MODSEQ, FLAGS.
        atts = []
        if include_uid:
            mb = self.store.get(self.selected) if self.selected else None
            if uid is None:
                uid = mb.view_uid(m) if isinstance(mb, LabelView) else m.uid
            atts.append(b"UID %d" % uid)
        if self.condstore:
            atts.append(b"MODSEQ (%d)" % m.modseq)
        if flags:
            atts.append(b"FLAGS (" + " ".join(sorted(m.flags)).encode() + b")")
        if self.p.gmail:
            atts.append(b"X-GM-LABELS (" + _labels(m, self._own_label()) + b")")
        return b" ".join(atts)

    def _own_label(self) -> Optional[str]:
        mb = self.store.get(self.selected) if self.selected else None
        return mb.label if isinstance(mb, LabelView) else None

    # -- main loop --------------------------------------------------------------------------

    def handle(self):
        caps = self.p.preauth_capabilities
        self.send(f"* OK [CAPABILITY {caps}] {self.p.greeting}".encode())
        while not self.closed:
            try:
                line = self.read_line()
            except (EOFError, ConnectionResetError, OSError):
                return
            self.srv.log(self.sid, "C", line)
            try:
                if not self.dispatch(line):
                    return
            except ProtocolError as e:
                self.send(f"* BAD {e}".encode())
            except Exception as e:  # keep serving; the transcript shows what happened
                import traceback
                self.srv.log(self.sid, "!", traceback.format_exc().encode())
                try:
                    self.send(b"* BYE internal error: " + repr(e).encode())
                except Exception:
                    pass
                return

    def dispatch(self, line: bytes) -> bool:
        parts = line.split(b" ", 2)
        if len(parts) < 2:
            self.send(b"* BAD Error in IMAP command: Missing command")
            return True
        tag, cmd = parts[0], parts[1].upper().decode()
        args = Tokenizer(self, parts[2] if len(parts) > 2 else b"")
        uid_mode = False
        if cmd == "UID":
            sub = args.token()
            cmd = "UID " + (sub or "").upper()
            uid_mode = True
        self.commands.append(cmd)
        self.srv.fire("before_command", self, cmd, args.rest())
        if self.reject_next is not None:
            # Only for commands without literals: the arguments are never read.
            code, text = self.reject_next
            self.reject_next = None
            self.no(tag, text, code=code)
            return True
        handler = getattr(self, "cmd_" + cmd.replace(" ", "_").replace(".", "_").lower(), None)
        if handler is None:
            self.send(tag + b" BAD Error in IMAP command " + cmd.encode() + b": Unknown command.")
            return True
        keep_going = handler(tag, args, uid_mode) if cmd.startswith("UID ") else handler(tag, args)
        self.srv.fire("after_command", self, cmd, None)
        return keep_going is not False

    def ok(self, tag: bytes, text: str, code: Optional[str] = None):
        if code is None and self.sent_vanished and self.selected:
            # Dovecot reports the new HIGHESTMODSEQ on the tagged OK of any command whose
            # response carried a VANISHED (RFC 7162 3.2.7 allows it; Dovecot always does).
            code = "HIGHESTMODSEQ %d" % self.store.get(self.selected).highestmodseq
        self.sent_vanished = False
        if code:
            self.send(tag + f" OK [{code}] {text}".encode())
        else:
            self.send(tag + f" OK {text}".encode())

    def no(self, tag: bytes, text: str, code: Optional[str] = None):
        self.send(tag + (f" NO [{code}] {text}" if code else f" NO {text}").encode())

    def require_auth(self, tag) -> bool:
        if not self.authenticated:
            self.send(tag + b" BAD Error in IMAP command: Command not allowed before login")
            return False
        return True

    def require_selected(self, tag) -> bool:
        if not self.require_auth(tag):
            return False
        if self.selected is None:
            self.send(tag + b" BAD Error in IMAP command: No mailbox selected.")
            return False
        return True

    # -- any state --------------------------------------------------------------------------

    def cmd_capability(self, tag, args):
        caps = self.p.postauth_capabilities if self.authenticated else self.p.preauth_capabilities
        if self.srv.tls_context and not self.tls and "STARTTLS" not in caps:
            caps = "STARTTLS " + caps
        self.send(f"* CAPABILITY {caps}".encode())
        self.ok(tag, "Pre-login capabilities listed, post-login capabilities have more." if not self.authenticated else "Capability completed.")

    def cmd_noop(self, tag, args):
        self.flush_events()
        self.ok(tag, "NOOP completed.")

    def cmd_logout(self, tag, args):
        self.send(b"* BYE Logging out")
        self.ok(tag, "Logout completed.")
        return False

    def cmd_id(self, tag, args):
        self.id_seen = True
        self.send(f"* ID {self.p.id_response}".encode())
        self.ok(tag, "ID completed.")

    def cmd_starttls(self, tag, args):
        if not self.srv.tls_context or self.tls:
            self.send(tag + b" BAD STARTTLS not available")
            return
        self.ok(tag, "Begin TLS negotiation now.")
        self.connection = self.srv.tls_context.wrap_socket(self.connection, server_side=True)
        self.rfile = self.connection.makefile("rb")
        self.wfile = self.connection.makefile("wb")
        self.tls = True

    # -- auth -------------------------------------------------------------------------------

    def _login_ok(self, tag):
        self.authenticated = True
        self.ok(tag, self.p.login_ok_text, code=f"CAPABILITY {self.p.postauth_capabilities}")

    def cmd_login(self, tag, args):
        user = args.token()
        pw = args.token()
        if self.srv.credentials and (user, pw) != self.srv.credentials:
            self.no(tag, "Authentication failed.", code="AUTHENTICATIONFAILED")
            return
        self._login_ok(tag)

    def cmd_authenticate(self, tag, args):
        mech = (args.token() or "").upper()
        initial = args.token()
        if initial is None:
            self.send(b"+ ")
            initial = self.read_line().decode()
            self.srv.log(self.sid, "C", b"<sasl response>")
        try:
            decoded = base64.b64decode(initial)
        except Exception:
            decoded = b""
        if mech == "PLAIN":
            parts = decoded.split(b"\0")
            user, pw = (parts[1].decode(), parts[2].decode()) if len(parts) == 3 else ("", "")
            if self.srv.credentials and (user, pw) != self.srv.credentials:
                self.no(tag, "Authentication failed.", code="AUTHENTICATIONFAILED")
                return
        elif mech == "XOAUTH2":
            if self.srv.oauth_tokens is not None:
                m = re.search(rb"auth=Bearer ([^\x01]+)", decoded)
                if not m or m.group(1).decode() not in self.srv.oauth_tokens:
                    self.send(b"+ " + base64.b64encode(b'{"status":"400","schemes":"Bearer","scope":"https://mail.google.com/"}'))
                    self.read_line()
                    self.no(tag, "Invalid credentials (Failure)", code="AUTHENTICATIONFAILED")
                    return
        self._login_ok(tag)

    # -- mailbox management ---------------------------------------------------------------------

    def cmd_enable(self, tag, args):
        if not self.require_auth(tag):
            return
        enabled = []
        while not args.eof():
            cap = (args.token() or "").upper()
            caps = self.p.capability_set(True)
            if cap == "QRESYNC" and "QRESYNC" in caps:
                self.qresync = self.condstore = True
                enabled.append("QRESYNC")
            elif cap == "CONDSTORE" and "CONDSTORE" in caps:
                self.condstore = True
                enabled.append("CONDSTORE")
        self.send(("* ENABLED " + " ".join(enabled)).encode())
        self.ok(tag, "Enabled.")

    def cmd_namespace(self, tag, args):
        if not self.require_auth(tag):
            return
        self.send(f"* NAMESPACE {self.p.namespace}".encode())
        self.ok(tag, "Namespace completed.")

    def _list_line(self, verb: str, mb) -> bytes:
        # Dovecot's order: \Noselect, \HasChildren/\HasNoChildren, then SPECIAL-USE flags.
        special = [a for a in mb.attrs if a.lower() not in ("\\noselect", "\\haschildren", "\\hasnochildren", "\\noinferiors")]
        if not self.p.list_special_use or verb == "LSUB":
            special = special if verb == "LSUB" else []
        attrs = []
        if mb.noselect:
            attrs.append("\\Noselect")
        if verb != "LSUB":
            attrs.append("\\HasChildren" if self.store.has_children(mb.name) else "\\HasNoChildren")
        attrs += special
        return f'* {verb} ({" ".join(attrs)}) "{self.store.delimiter}" '.encode() + imapfmt.mailbox_name(mb.name)

    def _matches(self, pattern: str, name: str) -> bool:
        rx = "^" + re.escape(pattern).replace(r"\*", ".*").replace("%", "[^" + re.escape(self.store.delimiter) + "]*") + "$"
        return re.match(rx, name, re.I if name.upper() == "INBOX" else 0) is not None

    def cmd_list(self, tag, args, verb="LIST"):
        if not self.require_auth(tag):
            return
        ref = args.token() or ""
        pattern = args.token() or ""
        if isinstance(pattern, list):
            pattern = pattern[0] if pattern else ""
        full = pattern if not ref else ref + pattern
        if full == "":
            self.send(f'* {verb} (\\Noselect) "{self.store.delimiter}" ""'.encode())
        else:
            for mb in self.store.list():
                if self._matches(full, mb.name):
                    self.send(self._list_line(verb, mb))
                    if self.p.has("duplicate-list-lines") and "\\Sent" in mb.attrs:
                        self.send(self._list_line(verb, mb))
                    if self.p.has("inbox-case-variant") and mb.name == "INBOX":
                        self.send(self._list_line(verb, mb).replace(b'"INBOX"', b'"Inbox"'))
        self.ok(tag, f"{verb} completed.")

    def cmd_xlist(self, tag, args):
        return self.cmd_list(tag, args, verb="XLIST")

    def cmd_lsub(self, tag, args):
        if not self.require_auth(tag):
            return
        args.token(); pattern = args.token() or "*"
        for mb in self.store.list():
            if mb.subscribed and self._matches(pattern, mb.name):
                self.send(self._list_line("LSUB", mb))
        self.ok(tag, "Lsub completed.")

    def cmd_create(self, tag, args):
        if not self.require_auth(tag):
            return
        name = args.token()
        if self.store.get(name):
            self.no(tag, "Mailbox already exists.", code="ALREADYEXISTS")
            return
        d = self.store.delimiter
        parts = name.rstrip(d).split(d)
        for i in range(1, len(parts)):
            parent = d.join(parts[:i])
            if not self.store.get(parent):
                self.store.create(parent, ["\\Noselect"], noselect=True, subscribed=False)
        self.store.create(name)
        self.ok(tag, "Create completed.")

    def cmd_delete(self, tag, args):
        if not self.require_auth(tag):
            return
        name = args.token()
        if not self.store.get(name):
            self.no(tag, "Mailbox doesn't exist.", code="NONEXISTENT")
            return
        self.store.delete(name)
        # a \Noselect parent that only existed to hold this child goes with it (Dovecot)
        d = self.store.delimiter
        parent = name.rsplit(d, 1)[0] if d in name else None
        while parent:
            pmb = self.store.get(parent)
            if pmb and pmb.noselect and not self.store.has_children(parent):
                self.store.delete(parent)
                parent = parent.rsplit(d, 1)[0] if d in parent else None
            else:
                break
        self.ok(tag, "Delete completed.")

    def cmd_rename(self, tag, args):
        if not self.require_auth(tag):
            return
        old, new = args.token(), args.token()
        if not self.store.get(old):
            self.no(tag, "Mailbox doesn't exist: " + str(old), code="NONEXISTENT")
            return
        if self.store.get(new):
            self.no(tag, "Mailbox already exists.", code="ALREADYEXISTS")
            return
        self.store.rename(old, new)
        self.ok(tag, "Rename completed.")

    def cmd_subscribe(self, tag, args):
        if not self.require_auth(tag):
            return
        mb = self.store.get(args.token())
        if mb:
            mb.subscribed = True
        self.ok(tag, "Subscribe completed.")

    def cmd_unsubscribe(self, tag, args):
        if not self.require_auth(tag):
            return
        mb = self.store.get(args.token())
        if mb:
            mb.subscribed = False
        self.ok(tag, "Unsubscribe completed.")

    def cmd_status(self, tag, args):
        if not self.require_auth(tag):
            return
        name = args.token()
        items = args.token() or []
        mb = self.store.get(name)
        if mb is None or mb.noselect:
            self.no(tag, "Mailbox doesn't exist: " + str(name))
            return
        wanted = {i.upper() for i in items}
        # STATUS on the mailbox this session has selected is answered from the session's own
        # (possibly stale) view, and Dovecot flags it with [CLIENTBUG] (RFC 3501 6.3.10 says
        # not to do this). mailsync does it on the background connection every pass.
        on_selected = mb.name == self.selected
        with self.store.lock:
            vals = []
            live = [mb.by_uid(u) or mb.tombstones.get(u) for u in self.view] if on_selected else mb.messages
            live = [m for m in live if m is not None]
            # Dovecot answers in this fixed order regardless of the order requested.
            if "MESSAGES" in wanted:
                vals += ["MESSAGES", str(len(live))]
            if "RECENT" in wanted:
                vals += ["RECENT", str(len(mb.recent))]
            if "UIDNEXT" in wanted and not self.p.has("status-omits-uidnext"):
                vals += ["UIDNEXT", str(mb.uidnext)]
            if "UIDVALIDITY" in wanted:
                vals += ["UIDVALIDITY", str(mb.uidvalidity)]
            if "UNSEEN" in wanted:
                vals += ["UNSEEN", str(sum(1 for m in live if "\\Seen" not in m.flags))]
            if "SIZE" in wanted:
                vals += ["SIZE", str(sum(m.size for m in mb.messages))]
            if "HIGHESTMODSEQ" in wanted and "CONDSTORE" in self.p.capability_set(True):
                vals += ["HIGHESTMODSEQ", str(mb.highestmodseq)]
        self.send(b"* STATUS " + imapfmt.mailbox_name(mb.name) + b" (" + " ".join(vals).encode() + b")")
        self.ok(tag, "Status completed.", code="CLIENTBUG" if on_selected else None)

    def cmd_select(self, tag, args, examine=False):
        if not self.require_auth(tag):
            return
        name = args.token()
        params = args.token() if not args.eof() else []
        mb = self.store.get(name)
        if mb is None or mb.noselect:
            self.selected = None
            self.no(tag, "Mailbox doesn't exist: " + str(name), code="NONEXISTENT")
            return
        if self.p.has("id-required-before-select") and not self.id_seen:
            self.no(tag, "SELECT Unsafe Login. Please contact kefu@188.com for help")
            return
        qresync_param = None
        if isinstance(params, list):
            for i, p in enumerate(params):
                if isinstance(p, str) and p.upper() == "CONDSTORE":
                    self.condstore = True
                if isinstance(p, str) and p.upper() == "QRESYNC" and i + 1 < len(params):
                    qresync_param = params[i + 1]
        with self.store.lock:
            if self.selected is not None:
                # RFC 7162 3.2.11: a CONDSTORE/QRESYNC-aware server says the previous mailbox
                # is closed before the new one's untagged data. Dovecot sends it unconditionally.
                self.send(b"* OK [CLOSED] Previous mailbox closed.")
            self.selected = mb.name
            with self.cv:
                self.pending.clear()
            self.view = mb.uids()
            self.send(b"* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)")
            if examine:
                self.send(b"* OK [PERMANENTFLAGS ()] Read-only mailbox.")
            else:
                self.send(b"* OK [PERMANENTFLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft \\*)] Flags permitted.")
            self.send(b"* %d EXISTS" % len(self.view))
            # \Recent belongs to the first session that selects the mailbox for writing
            if examine:
                self.recent_uids = set()
                self.send(b"* %d RECENT" % len(mb.recent))
            else:
                self.recent_uids = set(mb.recent)
                mb.recent.clear()
                self.send(b"* %d RECENT" % len(self.recent_uids))
            unseen_seq = next((i + 1 for i, u in enumerate(self.view) if "\\Seen" not in mb.by_uid(u).flags), None)
            if unseen_seq:
                self.send(b"* OK [UNSEEN %d] First unseen." % unseen_seq)
            self.send(b"* OK [UIDVALIDITY %d] UIDs valid" % mb.uidvalidity)
            self.send(b"* OK [UIDNEXT %d] Predicted next UID" % mb.uidnext)
            if "CONDSTORE" in self.p.capability_set(True):
                self.send(b"* OK [HIGHESTMODSEQ %d] Highest" % mb.highestmodseq)
            if qresync_param and self.qresync:
                self._qresync_select_report(mb, qresync_param)
        self.ok(tag, ("Examine" if examine else "Select") + " completed (0.001 + 0.000 secs).",
                code="READ-ONLY" if examine else "READ-WRITE")

    def _qresync_select_report(self, mb, param):
        try:
            uidvalidity, modseq = int(param[0]), int(param[1])
        except Exception:
            return
        if uidvalidity != mb.uidvalidity:
            return
        gone = [u for (u, ms) in mb.expunged if ms > modseq]
        if gone:
            self.send(b"* VANISHED (EARLIER) " + _seqset(gone).encode())
        for m in mb.messages:
            if m.modseq > modseq:
                self.send(b"* %d FETCH (" % (self.view.index(m.uid) + 1) + self._flags_atts(m, include_uid=True) + b")")

    def cmd_examine(self, tag, args):
        return self.cmd_select(tag, args, examine=True)

    def cmd_close(self, tag, args):
        if not self.require_selected(tag):
            return
        self.store.expunge(self.selected, only_deleted=True, origin=self)
        self.selected = None
        self.view = []
        self.ok(tag, "Close completed.")

    def cmd_unselect(self, tag, args):
        if not self.require_selected(tag):
            return
        self.selected = None
        self.view = []
        self.ok(tag, "Unselect completed.")

    def cmd_check(self, tag, args):
        self.flush_events()
        self.ok(tag, "Check completed.")

    def cmd_idle(self, tag, args):
        if not self.require_auth(tag):
            return
        self.send(b"+ idling")
        self.idling = self.has_idled = True
        self.srv.fire("idle_start", self, None, None)
        try:
            self.flush_events()
            while True:
                self.srv.fire("idle_tick", self, None, None)
                self.flush_events()
                if not self._readable(0.1):
                    with self.cv:
                        if not self.pending:
                            self.cv.wait(0.1)
                    continue
                line = self.rfile.readline()
                if not line:
                    raise EOFError
                self.srv.log(self.sid, "C", line.rstrip(b"\r\n"))
                if line.strip().upper() == b"DONE":
                    break
        finally:
            self.idling = False
        self.flush_events()
        if self.sent_vanished and self.selected:
            self.send(b"* OK [HIGHESTMODSEQ %d] Highest" % self.store.get(self.selected).highestmodseq)
            self.sent_vanished = False
        self.ok(tag, "Idle completed.")

    def _readable(self, timeout: float) -> bool:
        """True when a line can be read without blocking (reads are unbuffered)."""
        if isinstance(self.connection, ssl.SSLSocket) and self.connection.pending():
            return True
        r, _, _ = select.select([self.connection], [], [], timeout)
        return bool(r)

    # -- messages -------------------------------------------------------------------------------

    def cmd_append(self, tag, args):
        if not self.require_auth(tag):
            return
        name = args.token()
        flags, date = [], None
        tok = args.token()
        if isinstance(tok, list):
            flags = tok
            tok = args.token()
        if isinstance(tok, str) and re.match(r"^\d{1,2}-\w{3}-\d{4} ", tok):
            date = imapfmt.parse_internaldate(tok)
            tok = args.token()
        raw = tok if isinstance(tok, bytes) else (tok or "").encode()
        mb = self.store.get(name)
        if mb is None:
            self.no(tag, "Mailbox doesn't exist: " + str(name), code="TRYCREATE")
            return
        m = self.store.append(mb.name, raw, [f for f in flags if f != "\\Recent"], date, origin=self)
        if self.p.has("append-invisible-until-reselect"):
            with self.cv:
                self.pending = [e for e in self.pending if not (e.kind == "exists" and e.uids == [m.uid])]
        self.flush_events()
        self.ok(tag, "Append completed.", code=f"APPENDUID {mb.uidvalidity} {m.uid}" if self.uidplus else None)

    def cmd_expunge(self, tag, args):
        if not self.require_selected(tag):
            return
        self.store.expunge(self.selected, only_deleted=True, origin=self)
        self.flush_events()
        self.ok(tag, "Expunge completed.")

    def cmd_uid_expunge(self, tag, args, uid_mode):
        if not self.require_selected(tag):
            return
        mb = self.store.get(self.selected)
        uids = mb.resolve_set(args.token(), True)
        self.store.expunge(self.selected, uids, only_deleted=True, origin=self)
        self.flush_events()
        self.ok(tag, "Expunge completed.")

    def cmd_search(self, tag, args, uid_mode=False):
        if not self.require_selected(tag):
            return
        mb = self.store.get(self.selected)
        keys = []
        while not args.eof():
            keys.append(args.token())
        with self.store.lock:
            pos = {u: i + 1 for i, u in enumerate(self.view)}
            matched = [m for m in mb.messages if m.uid in pos and self._search_match(m, keys, mb)]
        ids = [m.uid if uid_mode else pos[m.uid] for m in matched]
        self.send(("* SEARCH " + " ".join(str(i) for i in ids)).encode())
        self.flush_events(allow_expunge=uid_mode)
        self.ok(tag, "Search completed.")

    def cmd_uid_search(self, tag, args, uid_mode):
        return self.cmd_search(tag, args, uid_mode=True)

    def _search_match(self, m: Message, keys: list, mb) -> bool:
        i = 0
        result = True
        while i < len(keys):
            k = keys[i]
            if isinstance(k, list):
                val = self._search_match(m, k, mb)
                i += 1
            elif isinstance(k, bytes):
                val = True
                i += 1
            else:
                up = k.upper()
                if up == "NOT":
                    val = not self._search_match(m, [keys[i + 1]], mb)
                    i += 2
                elif up == "OR":
                    val = self._search_match(m, [keys[i + 1]], mb) or self._search_match(m, [keys[i + 2]], mb)
                    i += 3
                elif up == "ALL":
                    val = True; i += 1
                elif up == "UID":
                    val = m.uid in mb.resolve_set(keys[i + 1], True); i += 2
                elif up in ("SEEN", "FLAGGED", "DELETED", "DRAFT", "ANSWERED"):
                    val = ("\\" + up.capitalize()) in m.flags; i += 1
                elif up in ("UNSEEN", "UNFLAGGED", "UNDELETED", "UNDRAFT", "UNANSWERED"):
                    val = ("\\" + up[2:].capitalize()) not in m.flags; i += 1
                elif up == "HEADER":
                    field, value = keys[i + 1], keys[i + 2]
                    hv = m.header(field) or ""
                    val = (value or "") == "" and hv != "" or (value or "").lower() in hv.lower()
                    i += 3
                elif up in ("SUBJECT", "FROM", "TO", "CC", "BCC"):
                    val = (keys[i + 1] or "").lower() in (m.header(up.capitalize()) or "").lower(); i += 2
                elif up in ("BODY", "TEXT"):
                    val = (keys[i + 1] or "").lower().encode() in m.raw.lower(); i += 2
                elif up in ("SINCE", "BEFORE", "ON", "SENTSINCE", "SENTBEFORE"):
                    d = datetime.strptime(keys[i + 1], "%d-%b-%Y").replace(tzinfo=timezone.utc)
                    val = (m.internaldate >= d) if "SINCE" in up else (m.internaldate < d)
                    i += 2
                elif up in ("LARGER", "SMALLER"):
                    n = int(keys[i + 1]); val = m.size > n if up == "LARGER" else m.size < n; i += 2
                elif up == "KEYWORD":
                    val = keys[i + 1] in m.flags; i += 2
                elif up == "X-GM-RAW" or up == "X-GM-LABELS":
                    val = keys[i + 1] in m.labels; i += 2
                elif up == "MODSEQ":
                    val = m.modseq >= int(keys[i + 1]); i += 2
                elif re.match(r"^[\d:*,]+$", k):
                    val = m.uid in mb.resolve_set(k, False, self.view); i += 1
                else:
                    val = True; i += 1
            result = result and val
        return result

    def cmd_store(self, tag, args, uid_mode=False):
        if not self.require_selected(tag):
            return
        mb = self.store.get(self.selected)
        spec = args.token()
        item = args.token()
        unchangedsince = None
        if isinstance(item, list):   # (UNCHANGEDSINCE n)
            if item and str(item[0]).upper() == "UNCHANGEDSINCE":
                unchangedsince = int(item[1])
            item = args.token()
        value = args.token()
        if not isinstance(value, list):
            value = [value]
        item = item.upper()
        silent = item.endswith(".SILENT")
        base = item.replace(".SILENT", "")
        op = {"+FLAGS": "add", "-FLAGS": "remove", "FLAGS": "replace",
              "+X-GM-LABELS": "add", "-X-GM-LABELS": "remove", "X-GM-LABELS": "replace"}[base]
        uids = mb.resolve_set(spec, uid_mode, self.view)
        failed = []
        if unchangedsince is not None:
            ok_uids = []
            for u in uids:
                m = mb.by_uid(u)
                (ok_uids if m.modseq <= unchangedsince else failed).append(u)
            uids = ok_uids
        if "X-GM-LABELS" in base:
            changed = self.store.set_labels(self.selected, uids, op, [v.strip('"') for v in value], origin=self)
        else:
            changed = self.store.store_flags(self.selected, uids, op, value, origin=self)
        for m in changed:
            u = mb.view_uid(m) if isinstance(mb, LabelView) else m.uid
            if u not in self.view:
                continue
            seq = self.view.index(u) + 1
            if not silent:
                self.send(b"* %d FETCH (" % seq + self._flags_atts(m, include_uid=uid_mode, uid=u) + b")")
            elif self.condstore:
                # RFC 7162 3.1.3: .SILENT suppresses FLAGS, not the MODSEQ update.
                self.send(b"* %d FETCH (" % seq + self._flags_atts(m, include_uid=uid_mode, flags=False, uid=u) + b")")
        self.flush_events(allow_expunge=uid_mode)
        if failed:
            self.ok(tag, "Conditional store failed", code=f"MODIFIED {_seqset(failed)}")
        else:
            self.ok(tag, "Store completed.")

    def cmd_uid_store(self, tag, args, uid_mode):
        return self.cmd_store(tag, args, uid_mode=True)

    def cmd_copy(self, tag, args, uid_mode=False, move=False):
        if not self.require_selected(tag):
            return
        mb = self.store.get(self.selected)
        spec = args.token()
        dest = args.token()
        dmb = self.store.get(dest)
        if dmb is None:
            self.no(tag, "Mailbox doesn't exist: " + str(dest), code="TRYCREATE")
            return
        uids = mb.resolve_set(spec, uid_mode, self.view)
        if not uids:
            self.ok(tag, "No messages found.")
            return
        if self.p.has("copyuid-permuted") and len(uids) > 1:
            # Assigned in rotated order; the COPYUID below still pairs the sorted sets.
            uids = uids[1:] + uids[:1]
        with self.store.lock:
            if move and self.p.gmail:
                pairs = self.store.move(self.selected, uids, dmb.name, origin=self)
            else:
                pairs = self.store.copy(self.selected, uids, dmb.name, origin=self)
            # COPYUID / APPENDUID are UIDPLUS response codes (RFC 4315 3); a server that does
            # not advertise UIDPLUS does not send them, which is what forces the engine's
            # search-the-destination fallback (TaskProcessor _resolveNewUIDs).
            code = f"COPYUID {dmb.uidvalidity} {_seqset([p[0] for p in pairs])} {_seqset([p[1] for p in pairs])}" if self.uidplus else None
            if move:
                self.send((f"* OK [{code}] Moved UIDs." if code else "* OK Moved UIDs.").encode())
                if not self.p.gmail:
                    self.store.expunge(self.selected, uids, origin=self)
        self.flush_events()
        if move:
            self.ok(tag, "Move completed.")
        else:
            self.ok(tag, "Copy completed.", code=code)

    def cmd_uid_copy(self, tag, args, uid_mode):
        return self.cmd_copy(tag, args, uid_mode=True)

    def cmd_move(self, tag, args, uid_mode=False):
        return self.cmd_copy(tag, args, uid_mode=uid_mode, move=True)

    def cmd_uid_move(self, tag, args, uid_mode):
        return self.cmd_copy(tag, args, uid_mode=True, move=True)

    def cmd_fetch(self, tag, args, uid_mode=False):
        if not self.require_selected(tag):
            return
        mb = self.store.get(self.selected)
        spec = args.token()
        items = args.token()
        if not isinstance(items, list):
            items = [items]
        # flatten macro names
        macros = {"ALL": ["FLAGS", "INTERNALDATE", "RFC822.SIZE", "ENVELOPE"],
                  "FAST": ["FLAGS", "INTERNALDATE", "RFC822.SIZE"],
                  "FULL": ["FLAGS", "INTERNALDATE", "RFC822.SIZE", "ENVELOPE", "BODY"]}
        if len(items) == 1 and isinstance(items[0], str) and items[0].upper() in macros:
            items = macros[items[0].upper()]
        changedsince, vanished = None, False
        if not args.eof():
            mods = args.token()
            if isinstance(mods, list):
                for i, mod in enumerate(mods):
                    if isinstance(mod, str) and mod.upper() == "CHANGEDSINCE":
                        changedsince = int(mods[i + 1])
                    if isinstance(mod, str) and mod.upper() == "VANISHED":
                        vanished = True
        wants_body = any(isinstance(i, str) and re.match(r"^(BODY(\.PEEK)?\[|RFC822(\.TEXT)?$)", i, re.I) for i in items)
        if wants_body:
            self.srv.fire("before_fetch_body", self, spec, None)
        with self.store.lock:
            # Dovecot answers a FETCH from the session's current view: messages another session
            # has expunged since this session last synced are still returned, and the EXPUNGE /
            # VANISHED lines follow the FETCH data (conformance: probe_stale_fetch). The engine
            # must therefore not trust a FETCH's row set as the set of live messages.
            uids = self._uids_in_view(spec, uid_mode)
            just_reported = []
            if changedsince is not None:
                if vanished and self.qresync:
                    just_reported = self.flush_events(allow_expunge=True)
                    # Dovecot answers VANISHED (EARLIER) from its expunge log for everything since
                    # the modseq, every time it is asked - it does not remember what this session
                    # was already told (conformance: changedsince_vanished_repeat).
                    gone = [u for (u, ms) in mb.expunged if ms > changedsince and u not in just_reported]
                    if self.p.has("vanished-earlier-not-repeated"):
                        gone = [u for u in gone if u in self.view]
                    if self.p.has("vanished-clipped-to-star") and uid_mode:
                        from .store import _parse_set
                        star = max(mb.uids(), default=0)
                        ranges = _parse_set(spec, star)
                        gone = [u for u in gone if any(lo <= u <= hi for lo, hi in ranges)]
                    if gone:
                        for u in gone:
                            if u in self.view:
                                self.view.remove(u)
                        self.send(b"* VANISHED (EARLIER) " + _seqset(gone).encode())
                uids = [u for u in uids if mb.by_uid(u) is not None and mb.by_uid(u).modseq > changedsince]
            pos = {u: i + 1 for i, u in enumerate(self.view)}
            for u in uids:
                m = mb.by_uid(u) or mb.tombstones.get(u)
                if m is None or u not in pos:
                    continue
                atts = self._fetch_atts(m, items, uid_mode, changedsince is not None, uid=u)
                self.send(b"* %d FETCH (" % pos[u] + atts + b")")
        self.flush_events(allow_expunge=uid_mode)
        self.ok(tag, "Fetch completed (0.001 + 0.000 secs).")

    def _uids_in_view(self, spec: str, uid_mode: bool) -> list:
        """Resolve a sequence set against this session's view (which may still hold messages
        that are already expunged on the server), in view order."""
        from .store import _parse_set
        if not self.view:
            return []
        out = []
        if uid_mode:
            top = self.view[-1]
            for lo, hi in _parse_set(spec, top):
                out += [u for u in self.view if lo <= u <= hi]
        else:
            n = len(self.view)
            for lo, hi in _parse_set(spec, n):
                out += [self.view[i - 1] for i in range(max(lo, 1), min(hi, n) + 1)]
        seen, uniq = set(), []
        for u in out:
            if u not in seen:
                seen.add(u)
                uniq.append(u)
        return uniq

    def cmd_uid_fetch(self, tag, args, uid_mode):
        return self.cmd_fetch(tag, args, uid_mode=True)

    def _fetch_atts(self, m: Message, items: list, uid_mode: bool, force_modseq: bool, uid: Optional[int] = None) -> bytes:
        out = []
        seen_uid = False
        uid = m.uid if uid is None else uid   # a Gmail label view has its own UIDs
        for it in items:
            if not isinstance(it, str):
                continue
            up = it.upper()
            if up == "UID":
                seen_uid = True
                out.append(b"UID %d" % uid)
            elif up == "FLAGS":
                out.append(b"FLAGS (" + " ".join(sorted(m.flags)).encode() + b")")
            elif up == "MODSEQ":
                out.append(b"MODSEQ (%d)" % m.modseq)
                force_modseq = False
            elif up == "INTERNALDATE":
                out.append(b"INTERNALDATE " + imapfmt.internaldate(m.internaldate).encode())
            elif up == "RFC822.SIZE":
                out.append(b"RFC822.SIZE %d" % m.size)
            elif up == "ENVELOPE":
                out.append(b"ENVELOPE " + imapfmt.envelope(m.parsed))
            elif up == "BODYSTRUCTURE":
                out.append(b"BODYSTRUCTURE " + imapfmt.bodystructure(m.parsed, extended=True))
            elif up == "BODY":
                out.append(b"BODY " + imapfmt.bodystructure(m.parsed, extended=False))
            elif up == "RFC822":
                out.append(b"RFC822 " + _literal(m.raw))
            elif up == "RFC822.HEADER":
                out.append(b"RFC822.HEADER " + _literal(imapfmt.split_header_body(m.raw)[0]))
            elif up == "RFC822.TEXT":
                out.append(b"RFC822.TEXT " + _literal(imapfmt.split_header_body(m.raw)[1]))
            elif up == "X-GM-MSGID":
                out.append(b"X-GM-MSGID %d" % m.gm_msgid)
            elif up == "X-GM-THRID":
                out.append(b"X-GM-THRID %d" % m.gm_thrid)
            elif up == "X-GM-LABELS":
                out.append(b"X-GM-LABELS (" + _labels(m, self._own_label()) + b")")
            elif up.startswith("BODY.PEEK[") or up.startswith("BODY["):
                mm = re.match(r"^BODY(?:\.PEEK)?\[(.*)\](?:<(\d+)\.(\d+)>)?$", it, re.I | re.S)
                sec, start, count = mm.group(1), mm.group(2), mm.group(3)
                data = imapfmt.section(m.raw, m.parsed, sec)
                if self.p.has("empty-part-with-calendar") and re.match(r"^\d", sec) and b"text/calendar" in m.raw.lower():
                    data = None
                label = b"BODY[" + _section_label(sec).encode() + b"]"
                if start is not None:
                    label += b"<%d>" % int(start)
                    data = data[int(start):int(start) + int(count)] if data is not None else None
                out.append(label + b" " + (_literal(data) if data is not None else b"NIL"))
                if not up.startswith("BODY.PEEK") and "\\Seen" not in m.flags:
                    self.store.store_flags(self.selected, [m.uid], "add", ["\\Seen"], origin=self)
                    out.append(b"FLAGS (" + " ".join(sorted(m.flags)).encode() + b")")
        if uid_mode and not seen_uid:
            out.insert(0, b"UID %d" % uid)
        if force_modseq and self.condstore:
            out.append(b"MODSEQ (%d)" % m.modseq)
        return b" ".join(out)


def _section_label(sec: str) -> str:
    """Dovecot echoes HEADER.FIELDS field names in upper case."""
    m = re.match(r"^(.*HEADER\.FIELDS(?:\.NOT)?) \((.*)\)$", sec, re.I)
    if m:
        return m.group(1).upper() + " (" + m.group(2).upper() + ")"
    return sec


def _literal(data: bytes) -> bytes:
    return b"{%d}\r\n" % len(data) + data


def _labels(m: Message, exclude: Optional[str] = None) -> bytes:
    # Labels are astrings (libetpan xgmlabels.c parses them with mailimap_astring_parse), so a
    # system label goes out quoted with its backslash escaped: "\\Inbox". Gmail's documentation
    # shows bare \Inbox, but that is not a valid astring and libetpan rejects it.
    labels = sorted(l for l in m.labels if l != exclude)
    return b" ".join(imapfmt.astring(l) for l in labels)


def _seqset(uids) -> str:
    uids = sorted(set(uids))
    parts, i = [], 0
    while i < len(uids):
        j = i
        while j + 1 < len(uids) and uids[j + 1] == uids[j] + 1:
            j += 1
        parts.append(str(uids[i]) if i == j else f"{uids[i]}:{uids[j]}")
        i = j + 1
    return ",".join(parts)


class _TCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class FakeImapServer:
    """Runs the fake server in a background thread. Use as a context manager, or start()/stop().

    hooks: name -> [fn(session, arg1, arg2)]. Names: before_command(cmd, rest), after_command(cmd, None),
    idle_start, idle_tick, before_fetch_body(spec). Hooks run on the session's thread while the
    store lock is NOT held, so they may mutate the store; the resulting notifications reach every
    session at its next opportunity, including the one the hook ran on. A hook that returns
    False declined this occasion (a filter did not match) and, if registered once, stays armed.
    """

    def __init__(self, personality="dovecot", store: Optional[Store] = None, host="127.0.0.1", port=0,
                 credentials: Optional[tuple] = ("test", "pass"), tls_context: Optional[ssl.SSLContext] = None,
                 implicit_tls: bool = False, log_path: Optional[str] = None):
        self.personality = get_personality(personality) if isinstance(personality, str) else personality
        self.store = store or (GmailStore() if self.personality.gmail else Store(delimiter=self.personality.delimiter))
        if not self.store.mailboxes:
            for name, attrs in self.personality.mailboxes:
                self.store.create(name, attrs, noselect="\\Noselect" in attrs, subscribed=(name != "INBOX"))
        self.host, self.port = host, port
        self.credentials = credentials
        self.oauth_tokens: Optional[set] = None
        self.tls_context = tls_context
        self.implicit_tls = implicit_tls
        self.lock = threading.Lock()
        self.sessions: list = []
        self.hooks: dict = {}
        self.transcript: list = []
        self._log_file = open(log_path, "a", buffering=1) if log_path else None
        self._server = None
        self._thread = None
        self.t0 = time.time()

    def log(self, sid: int, direction: str, line: bytes):
        if isinstance(line, bytes):
            line = line.decode("utf-8", "replace")
        entry = (time.time() - self.t0, sid, direction, line)
        self.transcript.append(entry)
        if self._log_file and not self._log_file.closed:
            try:
                self._log_file.write(f"{entry[0]:8.3f} {direction}{sid} {line}\n")
            except ValueError:
                pass  # a session finishing after stop() closed the file

    def add_hook(self, name: str, fn: Callable, once: bool = False):
        self.hooks.setdefault(name, []).append((fn, once))

    def fire(self, name: str, session, a, b):
        for fn, once in list(self.hooks.get(name, [])):
            fired = True
            try:
                fired = fn(session, a, b) is not False
            finally:
                if once and fired:
                    self.hooks[name].remove((fn, once))

    def start(self):
        srv = self
        self._server = _TCPServer((self.host, self.port), Session)
        self._server.owner = self
        if self.tls_context and self.implicit_tls:
            raw_accept = self._server.get_request

            def get_request():
                sock, addr = raw_accept()
                return srv.tls_context.wrap_socket(sock, server_side=True), addr
            self._server.get_request = get_request
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
        if self._log_file:
            self._log_file.close()

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()

    # -- helpers for tests ------------------------------------------------------------------

    def idling_sessions(self) -> list:
        return [s for s in self.sessions if s.idling and not s.closed]

    def wait_for_idle(self, timeout: float = 30) -> "Session":
        deadline = time.time() + timeout
        while time.time() < deadline:
            idle = self.idling_sessions()
            if idle:
                return idle[0]
            time.sleep(0.05)
        raise TimeoutError("no session entered IDLE")

    def commands_seen(self) -> list:
        return [(t, sid, line) for (t, sid, d, line) in self.transcript if d == "C"]
