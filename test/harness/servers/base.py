"""
What a scenario may ask of the server it runs against, regardless of whether that server
is the in-process fake or a real server in a container. Mutations model what another client
(webmail, a phone) does to the mailbox while mailsync is running. `truth()` is the same
placements structure harness.db builds from the engine's database, read back over IMAP
so the same code answers for every server kind, real providers included.
"""
import imaplib
import re
from abc import ABC, abstractmethod
from typing import Iterable, Optional

Truth = dict  # mailbox -> {uid: {"message_id": str, "flags": set, "labels": set}}


def normalize_message_id(mid: str) -> str:
    return (mid or "").strip().strip("<>").strip()


class Server(ABC):
    kind: str = ""
    username: str = "test"
    password: str = "pass"
    host: str = "127.0.0.1"
    port: int = 0
    gmail: bool = False

    # -- lifecycle
    @abstractmethod
    def start(self): ...

    @abstractmethod
    def stop(self): ...

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    # -- namespace
    # Scenarios name mailboxes as a flat-namespace server spells them ("Archive", "a/b").
    # A server that roots personal folders elsewhere maps those names to its own paths for
    # every operation, and maps its paths (from LIST or the engine's Folder.path) back, so
    # the harness compares and reports in scenario names on every server kind.
    def server_path(self, name: str) -> str:
        return name

    def scenario_name(self, path: str) -> str:
        return path

    # -- account
    def account_kwargs(self) -> dict:
        """Keyword arguments for harness.mailsync.account_json."""
        return dict(imap_host=self.host, imap_port=self.port, username=self.username, password=self.password)

    # -- population / mutation
    @abstractmethod
    def create_mailbox(self, name: str, special_use: Optional[str] = None): ...

    @abstractmethod
    def delete_mailbox(self, name: str): ...

    @abstractmethod
    def append(self, mailbox: str, raw: bytes, flags: Iterable[str] = ("\\Seen",)) -> int: ...

    def populate(self, mailbox: str, raws: Iterable[bytes], flags: Iterable[str] = ("\\Seen",)) -> list:
        return [self.append(mailbox, r, flags) for r in raws]

    @abstractmethod
    def expunge(self, mailbox: str, uids: Iterable[int]): ...

    @abstractmethod
    def set_flags(self, mailbox: str, uids: Iterable[int], add: Iterable[str] = (), remove: Iterable[str] = (),
                  per_message: bool = False): ...

    @abstractmethod
    def move(self, mailbox: str, uids: Iterable[int], dest: str): ...

    @abstractmethod
    def copy(self, mailbox: str, uids: Iterable[int], dest: str): ...

    @abstractmethod
    def set_uidvalidity(self, mailbox: str, value: int): ...

    @abstractmethod
    def set_uidnext(self, mailbox: str, value: int): ...

    def set_labels(self, mailbox: str, uids: Iterable[int], add: Iterable[str] = (), remove: Iterable[str] = ()):
        raise NotImplementedError(f"{self.kind} has no Gmail labels")

    def duplicate(self, mailbox: str, uids: Iterable[int], dest: str):
        """Same bytes, second physical copy. Default: copy over IMAP semantics."""
        self.copy(mailbox, uids, dest)

    def drop_connections(self):
        raise NotImplementedError(f"{self.kind} cannot drop connections")

    # -- observation
    def truth(self) -> Truth:
        return truth_via_imap(self.host, self.port, self.username, self.password, gmail=self.gmail)

    def mailboxes(self) -> list:
        return list(self.truth().keys())

    def uids(self, mailbox: str) -> list:
        return sorted(self.truth().get(mailbox, {}))


# Data items come in whatever order the server chooses (Dovecot: UID FLAGS; Cyrus: FLAGS UID).
_FETCH_HEAD = re.compile(rb"^\d+ \(.*BODY\[HEADER\.FIELDS \(MESSAGE-ID\)\] \{\d+\}$")
_FETCH_UID = re.compile(rb"[( ]UID (\d+)")
_FETCH_FLAGS = re.compile(rb"[( ]FLAGS \(([^)]*)\)")
_FETCH_LABELS = re.compile(rb"[( ]X-GM-LABELS \((.*?)\)")


def truth_via_imap(host: str, port: int, user: str, password: str, gmail: bool = False,
                   ssl: bool = False) -> Truth:
    cls = imaplib.IMAP4_SSL if ssl else imaplib.IMAP4
    conn = cls(host, port)
    try:
        conn.login(user, password)
        typ, boxes = conn.list()
        out: Truth = {}
        for entry in boxes or []:
            if not entry:
                continue
            m = re.match(rb'^\((?P<attrs>[^)]*)\) "?(?P<delim>[^" ]*)"? (?P<name>.*)$', entry)
            if not m:
                continue
            attrs = m["attrs"].decode().lower().split()
            name = m["name"].decode("utf-8", "surrogateescape")
            if name.startswith('"') and name.endswith('"'):
                name = name[1:-1].replace('\\"', '"').replace("\\\\", "\\")
            if "\\noselect" in attrs:
                continue
            out[name] = _fetch_mailbox(conn, name, gmail)
        return out
    finally:
        try:
            conn.logout()
        except Exception:
            pass


def _fetch_mailbox(conn, name: str, gmail: bool) -> dict:
    quoted = '"' + name.replace("\\", "\\\\").replace('"', '\\"') + '"'
    typ, data = conn.select(quoted, readonly=True)
    if typ != "OK":
        return {}
    if int(data[0] or 0) == 0:
        return {}
    items = "(UID FLAGS X-GM-LABELS BODY.PEEK[HEADER.FIELDS (MESSAGE-ID)])" if gmail else "(UID FLAGS BODY.PEEK[HEADER.FIELDS (MESSAGE-ID)])"
    typ, data = conn.uid("FETCH", "1:*", items)
    out = {}
    for part in data:
        if not isinstance(part, tuple):
            continue
        head, body = part
        if not _FETCH_HEAD.match(head):
            continue
        uid_m, flags_m, labels_m = _FETCH_UID.search(head), _FETCH_FLAGS.search(head), _FETCH_LABELS.search(head)
        if not uid_m or not flags_m:
            continue
        uid = int(uid_m.group(1))
        flags = {f.decode() for f in flags_m.group(1).split()} - {"\\Recent"}
        labels = set()
        if labels_m:
            labels = {l.strip('"') for l in re.findall(rb'"[^"]*"|\S+', labels_m.group(1))}
            labels = {l.decode() if isinstance(l, bytes) else l for l in labels}
        mid = ""
        for line in body.split(b"\r\n"):
            if line.lower().startswith(b"message-id:"):
                mid = normalize_message_id(line.split(b":", 1)[1].decode("utf-8", "replace"))
        out[uid] = {"message_id": mid, "flags": flags, "labels": labels}
    return out
