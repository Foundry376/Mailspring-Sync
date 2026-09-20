"""
A small SMTP server for exercising the engine's send path: EHLO/HELO, AUTH PLAIN/LOGIN,
MAIL/RCPT/DATA, and optional Postfix-style HELO validation (PR #135). Delivered messages are
kept in memory for assertions, and an `inbox` Store mailbox can be given to model a server
that delivers mail the user sends to themself back into their own INBOX.
"""
import base64
import re
import socketserver
import threading
import time
from typing import Optional


class SmtpMessage:
    def __init__(self, helo: str, sender: str, recipients: list, raw: bytes):
        self.helo, self.sender, self.recipients, self.raw = helo, sender, recipients, raw
        self.received_at = time.time()

    def header(self, name: str) -> Optional[str]:
        head = self.raw.split(b"\r\n\r\n", 1)[0]
        for line in head.split(b"\r\n"):
            if line.lower().startswith(name.lower().encode() + b":"):
                return line.split(b":", 1)[1].strip().decode("utf-8", "replace")
        return None


def helo_is_fqdn(name: str) -> bool:
    """Postfix reject_non_fqdn_helo_hostname / reject_invalid_helo_hostname semantics
    (RFC 5321 4.1.1.1 / 4.1.3): a dotted hostname of valid labels, or an address literal."""
    if re.match(r"^\[(IPv6:[0-9a-fA-F:.]+|\d{1,3}(\.\d{1,3}){3})\]$", name):
        return True
    if not re.match(r"^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$", name):
        return False
    labels = name.split(".")
    if any(l.startswith("-") or l.endswith("-") or len(l) > 63 for l in labels):
        return False
    return not labels[-1].isdigit()


class _Handler(socketserver.StreamRequestHandler):
    rbufsize = 0

    def send(self, line: str):
        self.server.owner.log("S", line)
        self.wfile.write((line + "\r\n").encode())

    def handle(self):
        srv: "FakeSmtpServer" = self.server.owner
        self.send(f"220 {srv.hostname} ESMTP ready")
        helo = None
        sender = None
        rcpts = []
        authed = not srv.require_auth
        while True:
            line = self.rfile.readline()
            if not line:
                return
            line = line.rstrip(b"\r\n").decode("utf-8", "replace")
            srv.log("C", line)
            verb, _, arg = line.partition(" ")
            verb = verb.upper()
            if verb in ("EHLO", "HELO"):
                if srv.reject_non_fqdn_helo and not helo_is_fqdn(arg.strip()):
                    self.send(f"504 5.5.2 <{arg.strip()}>: Helo command rejected: need fully-qualified hostname")
                    continue
                helo = arg.strip()
                if verb == "EHLO":
                    self.send(f"250-{srv.hostname}")
                    self.send("250-SIZE 35882577")
                    self.send("250-8BITMIME")
                    self.send("250-AUTH PLAIN LOGIN")
                    self.send("250 ENHANCEDSTATUSCODES")
                else:
                    self.send(f"250 {srv.hostname}")
            elif verb == "AUTH":
                mech, _, initial = arg.partition(" ")
                if mech.upper() == "PLAIN":
                    if not initial:
                        self.send("334 ")
                        initial = self.rfile.readline().strip().decode()
                    parts = base64.b64decode(initial).split(b"\0")
                    ok = len(parts) == 3 and (parts[1].decode(), parts[2].decode()) == srv.credentials
                elif mech.upper() == "LOGIN":
                    self.send("334 VXNlcm5hbWU6")
                    user = base64.b64decode(self.rfile.readline().strip()).decode()
                    self.send("334 UGFzc3dvcmQ6")
                    pw = base64.b64decode(self.rfile.readline().strip()).decode()
                    ok = (user, pw) == srv.credentials
                else:
                    self.send("504 5.5.4 Unrecognized authentication type")
                    continue
                if ok:
                    authed = True
                    self.send("235 2.7.0 Authentication successful")
                else:
                    self.send("535 5.7.8 Error: authentication failed")
            elif verb == "MAIL":
                if not authed:
                    self.send("530 5.7.0 Authentication required")
                    continue
                m = re.search(r"<([^>]*)>", arg)
                sender = m.group(1) if m else arg.split(":", 1)[-1].strip()
                rcpts = []
                self.send("250 2.1.0 Ok")
            elif verb == "RCPT":
                m = re.search(r"<([^>]*)>", arg)
                rcpts.append(m.group(1) if m else arg.split(":", 1)[-1].strip())
                self.send("250 2.1.5 Ok")
            elif verb == "DATA":
                self.send("354 End data with <CR><LF>.<CR><LF>")
                data = b""
                while True:
                    l = self.rfile.readline()
                    if not l or l == b".\r\n":
                        break
                    data += l[1:] if l.startswith(b"..") else l
                msg = SmtpMessage(helo or "", sender or "", list(rcpts), data)
                with srv.lock:
                    srv.messages.append(msg)
                srv.deliver(msg)
                self.send("250 2.0.0 Ok: queued as " + str(len(srv.messages)))
            elif verb == "RSET":
                sender, rcpts = None, []
                self.send("250 2.0.0 Ok")
            elif verb == "NOOP":
                self.send("250 2.0.0 Ok")
            elif verb == "QUIT":
                self.send("221 2.0.0 Bye")
                return
            else:
                self.send("502 5.5.2 Error: command not recognized")


class _TCP(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class FakeSmtpServer:
    def __init__(self, host="127.0.0.1", port=0, credentials=("test", "pass"), hostname="mail.example.test",
                 require_auth=True, reject_non_fqdn_helo=False, deliver_to=None):
        """deliver_to: optional (Store, mailbox, address) - messages sent to `address` are appended
        to that mailbox, as a real server delivers self-addressed mail back to the sender."""
        self.host, self.port = host, port
        self.credentials = credentials
        self.hostname = hostname
        self.require_auth = require_auth
        self.reject_non_fqdn_helo = reject_non_fqdn_helo
        self.deliver_to = deliver_to
        self.messages: list = []
        self.lock = threading.Lock()
        self.transcript: list = []
        self._server = None

    def log(self, direction, line):
        self.transcript.append((time.time(), direction, line))

    def deliver(self, msg: SmtpMessage):
        if not self.deliver_to:
            return
        store, mailbox, address = self.deliver_to
        if any(r.lower() == address.lower() for r in msg.recipients):
            store.append(mailbox, msg.raw, [])

    def start(self):
        self._server = _TCP((self.host, self.port), _Handler)
        self._server.owner = self
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True).start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            self._server = None

    def wait_for_message(self, timeout: float = 30) -> SmtpMessage:
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                if self.messages:
                    return self.messages[-1]
            time.sleep(0.1)
        raise TimeoutError("no message was submitted over SMTP")
