"""
Population and mutation over IMAP (imaplib), i.e. exactly what another client would do.
Shared by the real-server adapters; each supplies `port`, `ssl` and, where the server's
namespace differs from the scenario's mailbox names, `server_path`.
"""
import imaplib
import sys
from pathlib import Path
from typing import Iterable

from .base import Server


class ImapClientServer(Server):
    ssl: bool = False
    smtp = None

    def attach_smtp(self, email: str = "test@example.test"):
        """Start the harness's SMTP server (fakeimap/smtp.py) in front of this server. Mail sent
        to `email` is APPENDed to INBOX over IMAP as the test user, which is how Fastmail and
        a Dovecot/Postfix install deliver self-addressed mail; the engine sees only IMAP."""
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
        from fakeimap.smtp import FakeSmtpServer
        self.smtp = FakeSmtpServer(credentials=(self.username, self.password), deliver_to=(self, "INBOX", email)).start()
        return self.smtp

    def detach_smtp(self):
        if self.smtp:
            self.smtp.stop()
            self.smtp = None

    def account_kwargs(self) -> dict:
        kw = super().account_kwargs()
        if self.smtp:
            kw["smtp_host"], kw["smtp_port"] = "127.0.0.1", self.smtp.port
        return kw

    def _client(self) -> imaplib.IMAP4:
        if self.ssl:
            import ssl as _ssl
            ctx = _ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = _ssl.CERT_NONE
            c = imaplib.IMAP4_SSL("127.0.0.1", self.port, ssl_context=ctx)
        else:
            c = imaplib.IMAP4("127.0.0.1", self.port)
        c.login(self.username, self.password)
        return c

    @staticmethod
    def _q(name: str) -> str:
        return '"' + name.replace("\\", "\\\\").replace('"', '\\"') + '"'

    def _qm(self, mailbox: str) -> str:
        """A scenario mailbox name, quoted as the server spells it."""
        return self._q(self.server_path(mailbox))

    def create_mailbox(self, name, special_use=None):
        c = self._client()
        try:
            typ, data = c.create(self._qm(name))
            if typ != "OK" and b"exists" not in (data[0] or b"").lower():
                raise RuntimeError(f"CREATE {name}: {data}")
            c.subscribe(self._qm(name))
        finally:
            c.logout()

    def delete_mailbox(self, name):
        c = self._client()
        try:
            typ, data = c.delete(self._qm(name))
            if typ != "OK":
                raise RuntimeError(f"DELETE {name}: {data}")
        finally:
            c.logout()

    def append(self, mailbox, raw, flags=("\\Seen",)):
        return self.populate(mailbox, [raw], flags)[0]

    def populate(self, mailbox, raws, flags=("\\Seen",)):
        c = self._client()
        uids = []
        try:
            flag_str = "(" + " ".join(flags) + ")" if flags else None
            for raw in raws:
                typ, data = c.append(self._qm(mailbox), flag_str, None, raw)
                if typ != "OK":
                    raise RuntimeError(f"APPEND to {mailbox} failed: {data}")
                # b'[APPENDUID 1789... 12] Append completed.'
                text = data[0].decode()
                uids.append(int(text.split("APPENDUID")[1].split("]")[0].split()[1]))
        finally:
            c.logout()
        return uids

    def _with_selected(self, mailbox, fn):
        c = self._client()
        try:
            typ, _ = c.select(self._qm(mailbox))
            if typ != "OK":
                raise RuntimeError(f"SELECT {mailbox} failed")
            return fn(c)
        finally:
            c.logout()

    def expunge(self, mailbox, uids):
        def go(c):
            c.uid("STORE", uid_set(uids), "+FLAGS.SILENT", "(\\Deleted)")
            c.uid("EXPUNGE", uid_set(uids))
        self._with_selected(mailbox, go)

    def set_flags(self, mailbox, uids, add=(), remove=(), per_message=False):
        def go(c):
            # per_message: one STORE per UID, so HIGHESTMODSEQ advances once per message -
            # what builds a large modseq gap the way per-message client activity does.
            targets = [[u] for u in uids] if per_message else [list(uids)]
            for group in targets:
                sset = uid_set(group)
                if add:
                    c.uid("STORE", sset, "+FLAGS.SILENT", "(" + " ".join(add) + ")")
                if remove:
                    c.uid("STORE", sset, "-FLAGS.SILENT", "(" + " ".join(remove) + ")")
        self._with_selected(mailbox, go)

    def move(self, mailbox, uids, dest):
        self._with_selected(mailbox, lambda c: c.uid("MOVE", uid_set(uids), self._qm(dest)))

    def copy(self, mailbox, uids, dest):
        self._with_selected(mailbox, lambda c: c.uid("COPY", uid_set(uids), self._qm(dest)))


def uid_set(uids: Iterable[int]) -> str:
    uids = sorted(set(uids))
    parts, i = [], 0
    while i < len(uids):
        j = i
        while j + 1 < len(uids) and uids[j + 1] == uids[j] + 1:
            j += 1
        parts.append(str(uids[i]) if i == j else f"{uids[i]}:{uids[j]}")
        i = j + 1
    return ",".join(parts)
