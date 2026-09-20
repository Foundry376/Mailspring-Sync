"""The in-process fake server behind the Server interface."""
import re
import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from fakeimap import FakeImapServer, Store  # noqa: E402
from fakeimap.smtp import FakeSmtpServer  # noqa: E402
from .base import Server


class FakeServer(Server):
    kind = "fake"

    def __init__(self, personality: str = "dovecot", log_path: Optional[str] = None, smtp: bool = False,
                 email: str = "test@example.test", reject_non_fqdn_helo: bool = False,
                 smtp_sent_copy: Optional[str] = None, **kw):
        self.personality_name = personality
        self.imap = FakeImapServer(personality, log_path=log_path, credentials=(self.username, self.password), **kw)
        self.gmail = self.imap.personality.gmail
        self.store: Store = self.imap.store
        self.email = email
        # Mail the user sends to themself is delivered back into INBOX, as a real MTA would.
        # `smtp_sent_copy` names a mailbox every submitted message is filed into as well
        # (Gmail's submission service saves sent mail under \Sent by itself).
        self.smtp = FakeSmtpServer(credentials=(self.username, self.password), reject_non_fqdn_helo=reject_non_fqdn_helo,
                                   deliver_to=(self.store, "INBOX", email),
                                   sent_copy=(self.store, smtp_sent_copy) if smtp_sent_copy else None) if smtp else None

    def start(self):
        self.imap.start()
        self.port = self.imap.port
        if self.smtp:
            self.smtp.start()
        return self

    def stop(self):
        self.imap.stop()
        if self.smtp:
            self.smtp.stop()

    def account_kwargs(self) -> dict:
        kw = super().account_kwargs()
        kw["email"] = self.email
        if self.smtp:
            kw["smtp_host"], kw["smtp_port"] = "127.0.0.1", self.smtp.port
        return kw

    def sent_messages(self) -> list:
        return list(self.smtp.messages) if self.smtp else []

    def create_mailbox(self, name, special_use=None):
        self.store.create(name, [special_use] if special_use else [])

    def append(self, mailbox, raw, flags=("\\Seen",)):
        return self.store.append(mailbox, raw, flags).uid

    def _as_client(self, mailbox):
        # The Dovecot adapter performs mutations through a client session that SELECTs the
        # mailbox, which claims its \Recent messages; mirror that so both behave alike.
        self.store.get(mailbox).recent.clear()

    def expunge(self, mailbox, uids):
        self._as_client(mailbox)
        self.store.expunge(mailbox, list(uids))

    def set_flags(self, mailbox, uids, add=(), remove=(), per_message=False):
        self._as_client(mailbox)
        if add:
            self.store.store_flags(mailbox, list(uids), "add", list(add), per_message=per_message)
        if remove:
            self.store.store_flags(mailbox, list(uids), "remove", list(remove), per_message=per_message)

    def set_labels(self, mailbox, uids, add=(), remove=()):
        self._as_client(mailbox)
        if add:
            self.store.set_labels(mailbox, list(uids), "add", list(add))
        if remove:
            self.store.set_labels(mailbox, list(uids), "remove", list(remove))

    def move(self, mailbox, uids, dest):
        self._as_client(mailbox)
        self.store.move(mailbox, list(uids), dest)

    def copy(self, mailbox, uids, dest):
        self._as_client(mailbox)
        self.store.copy(mailbox, list(uids), dest)

    def set_uidvalidity(self, mailbox, value):
        self.store.set_uidvalidity(mailbox, value)

    def set_uidnext(self, mailbox, value):
        self.store.set_uidnext(mailbox, value)

    def duplicate(self, mailbox, uids, dest):
        self.store.duplicate(mailbox, list(uids), dest)

    def drop_connections(self):
        for s in list(self.imap.sessions):
            if not s.closed:
                try:
                    s.connection.shutdown(2)
                    s.connection.close()
                except OSError:
                    pass

    def truth(self):
        # The store is the truth; reading it back over IMAP is what the conformance suite
        # does to prove the two agree.
        t = self.store.truth()
        from .base import normalize_message_id
        return {mb: {uid: {**v, "message_id": normalize_message_id(v["message_id"])} for uid, v in uids.items()}
                for mb, uids in t.items()}

    # fake-only scripting
    def at(self, hook: str, fn, once: bool = True, session: Optional[str] = None,
           command: Optional[str] = None, mailbox: Optional[str] = None):
        """Run fn(session) at a protocol moment: idle_start, idle_tick, before_fetch_body,
        before_command, after_command. Filters narrow which occasion counts: `session` is
        "foreground" (a connection that has idled - the engine's IDLE worker) or "background"
        (one that never has), `command` a regex on the command line (e.g. "UID FETCH 1:\\*")
        for before/after_command, `mailbox` the session's selected mailbox."""
        rx = re.compile(command) if command else None

        def wrapped(sess, a, b):
            if session == "foreground" and not sess.has_idled:
                return False
            if session == "background" and sess.has_idled:
                return False
            if mailbox is not None and sess.selected != mailbox:
                return False
            if rx is not None:
                line = a if isinstance(a, str) else ""
                if isinstance(b, bytes):
                    line += " " + b.decode("utf-8", "replace").strip()
                if not rx.search(line):
                    return False
            fn(sess)
            return True
        self.imap.add_hook(hook, wrapped, once=once)
