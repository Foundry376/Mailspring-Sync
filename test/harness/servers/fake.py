"""The in-process fake server behind the Server interface."""
import re
import sys
from datetime import date, timedelta
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from fakeimap import FakeImapServer, Store  # noqa: E402
from fakeimap.smtp import FakeSmtpServer  # noqa: E402
from fakeimap.graph import FakeGraphServer  # noqa: E402
from .base import Server


def _generated_events(count: int) -> list:
    """`count` one-off daily events from 2026-10-16, for scenarios about how many are read at once."""
    events = []
    for i in range(1, count + 1):
        day = (date(2026, 10, 15) + timedelta(days=i)).isoformat()
        events.append({"id": f"AAMkEvGen{i:02d}=", "changeKey": f"ck-gen-{i}", "iCalUId": f"uid-gen-{i:02d}",
                       "type": "singleInstance", "subject": f"Generated {i}", "isAllDay": False,
                       "start": {"dateTime": f"{day}T09:00:00.0000000", "timeZone": "Pacific Standard Time"},
                       "end": {"dateTime": f"{day}T09:30:00.0000000", "timeZone": "Pacific Standard Time"},
                       "originalStartTimeZone": "Pacific Standard Time",
                       "lastModifiedDateTime": "2026-10-01T12:00:00Z"})
    return events


class FakeServer(Server):
    kind = "fake"

    def __init__(self, personality: str = "dovecot", log_path: Optional[str] = None, smtp: bool = False,
                 email: str = "test@example.test", reject_non_fqdn_helo: bool = False,
                 smtp_sent_copy: Optional[str] = None, smtp_sent_copy_delay: float = 0,
                 smtp_auth_response: Optional[str] = None, graph: bool = False, graph_send: bool = True,
                 graph_consented: bool = True, graph_consent_refusals: int = 0,
                 graph_aliases: Optional[list] = None, graph_drop_token: bool = False, graph_sent_copy: Optional[str] = "Sent Items",
                 graph_sent_copy_delay: float = 0, graph_failures: Optional[list] = None,
                 graph_calendars: Optional[list] = None, graph_calendars_consented: bool = True,
                 graph_calendars_page_size: int = 0, graph_shared_mailbox_owner: Optional[str] = None,
                 graph_events: Optional[dict] = None, graph_events_page_size: int = 0,
                 graph_generated_events: int = 0, graph_provider: str = "office365", **kw):
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
                                   sent_copy=(self.store, smtp_sent_copy) if smtp_sent_copy else None,
                                   sent_copy_delay=smtp_sent_copy_delay,
                                   auth_response=smtp_auth_response) if smtp else None
        # `graph: true` makes the account a Microsoft OAuth account: IMAP and SMTP take the
        # fake's mail-scoped access tokens, and with `graph_send` (the default) mail goes out
        # through the fake Graph API, which files its Sent Items copy in `graph_sent_copy`.
        # `graph_shared_mailbox_owner` makes it a shared mailbox signed in as that address, and
        # `graph_provider: outlook` an Outlook.com account rather than Office 365.
        self.graph_send = graph and graph_send
        self.graph_shared_mailbox_owner = graph_shared_mailbox_owner
        self.graph_provider = graph_provider
        self.graph = FakeGraphServer(graph_consented=graph_consented, consent_refusals=graph_consent_refusals,
                                     mailbox=email, aliases=graph_aliases or [], drop_graph_token=graph_drop_token,
                                     sent_copy=(self.store, graph_sent_copy) if graph_sent_copy else None,
                                     sent_copy_delay=graph_sent_copy_delay, failures=graph_failures,
                                     deliver_to=(self.store, "INBOX", email), calendars=graph_calendars,
                                     calendars_consented=graph_calendars_consented,
                                     calendars_page_size=graph_calendars_page_size, events=graph_events,
                                     events_page_size=graph_events_page_size) if graph else None
        if self.graph and graph_generated_events:
            calendar_id = graph_calendars[0]["id"]
            self.graph.events.setdefault(calendar_id, []).extend(_generated_events(graph_generated_events))
        if self.graph:
            self.imap.oauth_tokens = self.graph.mail_tokens
            if self.smtp:
                self.smtp.oauth_tokens = self.graph.mail_tokens

    def start(self):
        self.imap.start()
        self.port = self.imap.port
        if self.smtp:
            self.smtp.start()
        if self.graph:
            self.graph.start()
        return self

    def stop(self):
        self.imap.stop()
        if self.smtp:
            self.smtp.stop()
        if self.graph:
            self.graph.stop()

    def account_kwargs(self) -> dict:
        kw = super().account_kwargs()
        kw["email"] = self.email
        if self.smtp:
            kw["smtp_host"], kw["smtp_port"] = "127.0.0.1", self.smtp.port
        if self.graph:
            kw["provider"] = self.graph_provider
            # Microsoft onboarding uses the address as both usernames; XOAUTH2 checks only the token.
            kw["username"] = self.email
            kw["extra_settings"] = {
                "refresh_client_id": "harness-client-id",
                "refresh_token": self.graph.refresh_token,
                "graph_send": self.graph_send,
            }
            if self.graph_shared_mailbox_owner:
                kw["extra_settings"]["smtp_username"] = self.graph_shared_mailbox_owner
        return kw

    def engine_env(self) -> dict:
        if not self.graph:
            return {}
        return {"MS_GRAPH_URL": self.graph.url, "MS_OAUTH_TOKEN_URL": self.graph.token_url,
                "IDENTITY_SERVER": self.graph.identity_url}

    def sent_messages(self) -> list:
        return list(self.smtp.messages) if self.smtp else []

    def create_mailbox(self, name, special_use=None):
        self.store.create(name, [special_use] if special_use else [])

    def delete_mailbox(self, name):
        self.store.delete(name)

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
