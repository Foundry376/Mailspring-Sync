"""
Mailbox state shared by every connection to the fake server.

Sequence numbers are a per-session view (Session.view), never a property of the
mailbox: two connections that have learned about different sets of messages must map
sequence numbers differently, exactly as on a real server. The store only knows UIDs.

Modseqs follow RFC 7162 the way Dovecot implements them: one counter per mailbox,
incremented by every flag change, append, and expunge; expunged UIDs are remembered
with the modseq at which they vanished so VANISHED (EARLIER) can be answered.
"""
import email
import email.policy
import email.utils
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Callable, Iterable, Optional

SYSTEM_FLAGS = {"\\Seen", "\\Answered", "\\Flagged", "\\Deleted", "\\Draft"}


class Message:
    __slots__ = ("uid", "flags", "modseq", "internaldate", "raw", "_parsed", "gm_msgid", "gm_thrid", "labels")

    def __init__(self, uid: int, raw: bytes, flags: Iterable[str], modseq: int,
                 internaldate: Optional[datetime] = None):
        self.uid = uid
        self.raw = raw if raw.endswith(b"\r\n") else raw + b"\r\n"
        self.flags = set(flags)
        self.modseq = modseq
        self.internaldate = internaldate or datetime.now(timezone.utc)
        self._parsed = None
        # Gmail personality only
        self.gm_msgid = 0
        self.gm_thrid = 0
        self.labels: set = set()

    @property
    def parsed(self):
        if self._parsed is None:
            self._parsed = email.message_from_bytes(self.raw, policy=email.policy.compat32)
        return self._parsed

    def header(self, name: str) -> Optional[str]:
        v = self.parsed.get(name)
        return None if v is None else str(v).replace("\r\n", "").replace("\n", "")

    @property
    def message_id(self) -> str:
        return (self.header("Message-ID") or "").strip()

    @property
    def size(self) -> int:
        return len(self.raw)

    def __repr__(self):
        return f"<Message uid={self.uid} flags={sorted(self.flags)} modseq={self.modseq} {self.message_id}>"


@dataclass
class StoreEvent:
    """Broadcast to sessions so they can emit untagged responses at their next opportunity."""
    kind: str            # exists | expunge | flags
    mailbox: str
    uids: list
    modseq: int
    origin: object = None   # the session that caused it, or None for out-of-band changes


class Mailbox:
    def __init__(self, name: str, attrs: Iterable[str] = (), uidvalidity: Optional[int] = None,
                 noselect: bool = False, subscribed: bool = True):
        self.name = name
        self.attrs = list(attrs)             # \Sent, \HasNoChildren, ... as strings
        self.uidvalidity = uidvalidity or int(time.time())
        self.uidnext = 1
        self.highestmodseq = 1
        self.messages: list = []             # ascending UID
        self._index: dict = {}               # uid -> Message
        self.expunged: list = []             # (uid, modseq at expunge)
        self.tombstones: dict = {}           # uid -> Message as it was when expunged
        self.noselect = noselect
        self.subscribed = subscribed
        self.recent: set = set()             # UIDs no session has yet seen (\Recent)
        self.gm_index: dict = {}             # gm_msgid -> Message (Gmail stores)
        self.permanentflags = sorted(SYSTEM_FLAGS) + ["\\*"]

    # -- queries -------------------------------------------------------------------------
    def by_uid(self, uid: int) -> Optional[Message]:
        return self._index.get(uid)

    def add(self, m: Message):
        self.messages.append(m)
        self._index[m.uid] = m

    def remove(self, m: Message):
        self.messages.remove(m)
        self._index.pop(m.uid, None)
        self.tombstones[m.uid] = m

    def reindex(self):
        self._index = {m.uid: m for m in self.messages}

    def uids(self) -> list:
        return [m.uid for m in self.messages]

    def unseen(self) -> int:
        return sum(1 for m in self.messages if "\\Seen" not in m.flags)

    def resolve_set(self, spec: str, uid_mode: bool, view: Optional[list] = None) -> list:
        """Expand an IMAP sequence-set into UIDs. In sequence mode `view` is the session's
        seqno->uid list. Nonexistent UIDs are silently skipped, as servers do."""
        out = []
        if uid_mode:
            import bisect
            existing = self.uids()
            top = existing[-1] if existing else 0
            for lo, hi in _parse_set(spec, top):
                out += existing[bisect.bisect_left(existing, lo):bisect.bisect_right(existing, hi)]
        else:
            view = view if view is not None else self.uids()
            n = len(view)
            for lo, hi in _parse_set(spec, n):
                out += [view[i - 1] for i in range(max(lo, 1), min(hi, n) + 1)]
        seen, uniq = set(), []
        for u in out:
            if u not in seen and self.by_uid(u) is not None:
                seen.add(u)
                uniq.append(u)
        return uniq

    def next_modseq(self) -> int:
        self.highestmodseq += 1
        return self.highestmodseq


def _parse_set(spec: str, star: int) -> list:
    ranges = []
    for part in spec.split(","):
        if ":" in part:
            a, b = part.split(":", 1)
        else:
            a = b = part
        lo = star if a == "*" else int(a)
        hi = star if b == "*" else int(b)
        if lo > hi:
            lo, hi = hi, lo
        ranges.append((lo, hi))
    return ranges


class Store:
    def __init__(self, delimiter: str = "/"):
        self.lock = threading.RLock()
        self.delimiter = delimiter
        self.mailboxes: dict = {}
        self._listeners: list = []
        self._gm_counter = 1 << 60
        self.transcript: list = []   # (t, "event", description) for debugging

    # -- listeners -----------------------------------------------------------------------
    def add_listener(self, fn: Callable[[StoreEvent], None]):
        self._listeners.append(fn)

    def remove_listener(self, fn):
        if fn in self._listeners:
            self._listeners.remove(fn)

    def _emit(self, ev: StoreEvent):
        self.transcript.append((time.time(), ev.kind, ev.mailbox, list(ev.uids), ev.modseq))
        for fn in list(self._listeners):
            fn(ev)

    # -- mailboxes -----------------------------------------------------------------------
    def create(self, name: str, attrs: Iterable[str] = (), **kw) -> Mailbox:
        with self.lock:
            if name in self.mailboxes:
                raise KeyError(f"mailbox exists: {name}")
            mb = Mailbox(name, attrs, **kw)
            self.mailboxes[name] = mb
            return mb

    def get(self, name: str) -> Optional[Mailbox]:
        if name.upper() == "INBOX":
            name = "INBOX"
        return self.mailboxes.get(name)

    def delete(self, name: str):
        with self.lock:
            del self.mailboxes[name]

    def rename(self, old: str, new: str):
        with self.lock:
            mb = self.mailboxes.pop(old)
            mb.name = new
            self.mailboxes[new] = mb
            prefix = old + self.delimiter
            for child in [n for n in self.mailboxes if n.startswith(prefix)]:
                c = self.mailboxes.pop(child)
                c.name = new + child[len(old):]
                self.mailboxes[c.name] = c

    def list(self) -> list:
        return list(self.mailboxes.values())

    def has_children(self, name: str) -> bool:
        prefix = name + self.delimiter
        return any(n.startswith(prefix) for n in self.mailboxes)

    # -- messages ------------------------------------------------------------------------
    def append(self, mailbox: str, raw: bytes, flags: Iterable[str] = (), internaldate=None,
               origin=None, labels: Iterable[str] = ()) -> Message:
        with self.lock:
            mb = self.get(mailbox)
            if mb is None:
                raise KeyError(mailbox)
            m = Message(mb.uidnext, raw, flags, mb.next_modseq(), internaldate)
            m.gm_msgid = self._gm_counter
            m.gm_thrid = self._gm_counter
            self._gm_counter += 1
            m.labels = set(labels)
            mb.uidnext += 1
            mb.add(m)
            mb.recent.add(m.uid)
            self._emit(StoreEvent("exists", mb.name, [m.uid], m.modseq, origin))
            return m

    def append_many(self, mailbox: str, raws: Iterable[bytes], flags: Iterable[str] = ("\\Seen",)) -> list:
        return [self.append(mailbox, r, flags) for r in raws]

    def store_flags(self, mailbox: str, uids: Iterable[str], op: str, flags: Iterable[str],
                    origin=None, per_message: bool = False) -> list:
        """op: add | remove | replace. Returns the changed messages. One STORE is one
        transaction: every message it changes gets the same, single new modseq, as on
        Dovecot (conformance probe_bulk_store_modseq); a no-op STORE neither bumps the
        modseq nor is reported. per_message models each message changed by a separate
        transaction (another client marking messages one at a time, a filter run), giving
        each its own modseq - which is how a modseq gap larger than one grows on a real
        server between two of the engine's syncs."""
        with self.lock:
            mb = self.get(mailbox)
            flags = set(flags)
            changed = []
            for uid in uids:
                m = mb.by_uid(uid)
                if m is None:
                    continue
                before = set(m.flags)
                if op == "add":
                    m.flags |= flags
                elif op == "remove":
                    m.flags -= flags
                else:
                    m.flags = set(flags)
                if m.flags == before:
                    continue
                if per_message:
                    m.modseq = mb.next_modseq()
                changed.append(m)
            if changed:
                if not per_message:
                    modseq = mb.next_modseq()
                    for m in changed:
                        m.modseq = modseq
                self._emit(StoreEvent("flags", mb.name, [m.uid for m in changed], mb.highestmodseq, origin))
            return changed

    def set_labels(self, mailbox: str, uids: Iterable[int], op: str, labels: Iterable[str], origin=None) -> list:
        with self.lock:
            mb = self.get(mailbox)
            labels = set(labels)
            changed = []
            for uid in uids:
                m = mb.by_uid(uid)
                if m is None:
                    continue
                if op == "add":
                    m.labels |= labels
                elif op == "remove":
                    m.labels -= labels
                else:
                    m.labels = set(labels)
                m.modseq = mb.next_modseq()
                changed.append(m)
            if changed:
                self._emit(StoreEvent("flags", mb.name, [m.uid for m in changed], mb.highestmodseq, origin))
            return changed

    def expunge(self, mailbox: str, uids: Optional[Iterable[int]] = None, only_deleted: bool = False,
                origin=None) -> list:
        """Remove messages. uids=None means every message (with only_deleted, every \\Deleted one)."""
        with self.lock:
            mb = self.get(mailbox)
            targets = set(uids) if uids is not None else set(mb.uids())
            gone = []
            for m in list(mb.messages):
                if m.uid in targets and (not only_deleted or "\\Deleted" in m.flags):
                    mb.remove(m)
                    gone.append(m)
            if not gone:
                return []
            modseq = mb.next_modseq()
            for m in gone:
                mb.expunged.append((m.uid, modseq))
                mb.recent.discard(m.uid)
            self._emit(StoreEvent("expunge", mb.name, [m.uid for m in gone], modseq, origin))
            return gone

    def copy(self, src: str, uids: Iterable[int], dst: str, origin=None) -> list:
        """Returns [(src uid, dst uid)] for COPYUID."""
        with self.lock:
            s = self.get(src)
            pairs = []
            for uid in uids:
                m = s.by_uid(uid)
                if m is None:
                    continue
                n = self.append(dst, m.raw, m.flags - {"\\Recent"}, m.internaldate, origin, labels=m.labels)
                pairs.append((uid, n.uid))
            return pairs

    def move(self, src: str, uids: Iterable[int], dst: str, origin=None) -> list:
        with self.lock:
            pairs = self.copy(src, uids, dst, origin)
            self.expunge(src, [p[0] for p in pairs], origin=origin)
            return pairs

    # -- test-side manipulation ------------------------------------------------------------
    def set_uidvalidity(self, mailbox: str, value: int, renumber: bool = True):
        """Simulate a mailbox rebuild: new UIDVALIDITY, and (by default) fresh UIDs from 1."""
        with self.lock:
            mb = self.get(mailbox)
            mb.uidvalidity = value
            if renumber:
                for i, m in enumerate(mb.messages, start=1):
                    m.uid = i
                mb.reindex()
                mb.uidnext = len(mb.messages) + 1
                mb.expunged.clear()

    def set_uidnext(self, mailbox: str, value: int):
        """Like doveadm mailbox update --min-next-uid: leaves a hole in the UID space."""
        with self.lock:
            mb = self.get(mailbox)
            mb.uidnext = max(mb.uidnext, value)

    def duplicate(self, src: str, uids: Iterable[int], dst: str, flags: Optional[Iterable[str]] = None) -> list:
        """A second physical copy of the same bytes in another mailbox, as Exchange auto-save,
        iCloud, and ProtonMail Bridge's All Mail produce. Same Message-ID, new UID."""
        with self.lock:
            s = self.get(src)
            out = []
            for uid in uids:
                m = s.by_uid(uid)
                if m is not None:
                    out.append(self.append(dst, m.raw, m.flags if flags is None else flags, m.internaldate))
            return out

    # -- truth ---------------------------------------------------------------------------
    def truth(self) -> dict:
        """{mailbox: {uid: {"message_id": ..., "flags": {...}}}} for comparison with the DB."""
        with self.lock:
            return {
                mb.name: {m.uid: {"message_id": m.message_id, "flags": set(m.flags) - {"\\Recent"},
                                  "labels": set(m.labels)} for m in mb.messages}
                for mb in self.mailboxes.values() if not mb.noselect
            }


# -- Gmail ---------------------------------------------------------------------------------
#
# Gmail keeps every message in [Gmail]/All Mail (or Spam / Trash) and exposes INBOX, Sent
# Mail, Starred, Important, Drafts and user labels as views selected by X-GM-LABELS. A view
# has its own UIDVALIDITY and its own UID for each member, assigned when the message gains
# the label; losing the label is an expunge from the view. Deleting from a view removes the
# label; deleting from All Mail moves to Trash; deleting from Trash/Spam is permanent.
# Reference: Gmail IMAP extensions documentation (X-GM-LABELS, X-GM-MSGID, X-GM-THRID) and
# MailUtils::roleForFolderViaPath's [gmail]/ handling.

GMAIL_SYSTEM_LABELS = {
    "INBOX": "\\Inbox",
    "[Gmail]/Sent Mail": "\\Sent",
    "[Gmail]/Starred": "\\Starred",
    "[Gmail]/Important": "\\Important",
    "[Gmail]/Drafts": "\\Draft",
}
GMAIL_PHYSICAL = ("[Gmail]/All Mail", "[Gmail]/Spam", "[Gmail]/Trash")


class LabelView(Mailbox):
    def __init__(self, name: str, attrs, label: str, backing: "Mailbox", uidvalidity: Optional[int] = None):
        super().__init__(name, attrs, uidvalidity)
        self.label = label
        self.backing = backing
        self.uid_of: dict = {}     # gm_msgid -> uid in this view
        self.msg_of: dict = {}     # uid -> gm_msgid

    @property
    def messages(self) -> list:
        members = [m for m in self.backing.messages if self.label in m.labels]
        for m in members:
            if m.gm_msgid not in self.uid_of:
                self.uid_of[m.gm_msgid] = self.uidnext
                self.msg_of[self.uidnext] = m.gm_msgid
                self.uidnext += 1
        return sorted(members, key=lambda m: self.uid_of[m.gm_msgid])

    @messages.setter
    def messages(self, value):
        pass  # computed

    @property
    def highestmodseq(self) -> int:
        return max([self._own_modseq] + [m.modseq for m in self.messages])

    @highestmodseq.setter
    def highestmodseq(self, v):
        self._own_modseq = v

    def next_modseq(self) -> int:
        self.backing.highestmodseq += 1
        return self.backing.highestmodseq

    def by_uid(self, uid: int) -> Optional[Message]:
        gm = self.msg_of.get(uid)
        if gm is None:
            return None
        m = self.backing.gm_index.get(gm)
        return m if m is not None and self.label in m.labels else None

    def uids(self) -> list:
        return [self.uid_of[m.gm_msgid] for m in self.messages]

    def view_uid(self, m: Message) -> int:
        self.messages  # assign
        return self.uid_of[m.gm_msgid]

    def add(self, m: Message):
        self.backing.add(m)

    def remove(self, m: Message):
        self.backing.remove(m)


class GmailStore(Store):
    """A Store whose INBOX and label folders are views of [Gmail]/All Mail."""

    def __init__(self, delimiter="/"):
        super().__init__(delimiter)
        self.gmail = True

    def create(self, name, attrs=(), **kw):
        attrs = list(attrs)
        if name in GMAIL_PHYSICAL or "\\Noselect" in attrs:
            if name in self.mailboxes:      # All Mail may have been created implicitly by a view
                return self.mailboxes[name]
            mb = super().create(name, attrs, **kw)
            mb.gm_index = {}
            return mb
        label = GMAIL_SYSTEM_LABELS.get(name, name)
        backing = self.mailboxes.get("[Gmail]/All Mail")
        if backing is None:
            backing = super().create("[Gmail]/All Mail", ["\\All"])
            backing.gm_index = {}
        with self.lock:
            view = LabelView(name, attrs, label, backing, kw.get("uidvalidity"))
            view.subscribed = kw.get("subscribed", True)
            self.mailboxes[name] = view
            return view

    def _physical(self, mb):
        return mb.backing if isinstance(mb, LabelView) else mb

    def _views_of(self, physical) -> list:
        return [v for v in self.mailboxes.values() if isinstance(v, LabelView) and v.backing is physical]

    def _label_events(self, physical, m: Message, before: set, after: set, modseq: int, origin):
        for v in self._views_of(physical):
            if v.label in after and v.label not in before:
                self._emit(StoreEvent("exists", v.name, [v.view_uid(m)], modseq, origin))
            elif v.label in before and v.label not in after:
                uid = v.uid_of.get(m.gm_msgid)
                if uid is not None:
                    v.expunged.append((uid, modseq))
                    self._emit(StoreEvent("expunge", v.name, [uid], modseq, origin))
            elif v.label in after:
                self._emit(StoreEvent("flags", v.name, [v.view_uid(m)], modseq, origin))

    def append(self, mailbox, raw, flags=(), internaldate=None, origin=None, labels=()):
        mb = self.get(mailbox)
        if isinstance(mb, LabelView):
            labels = set(labels) | {mb.label}
            if "\\Flagged" in flags:
                labels.add("\\Starred")
            if "\\Draft" in flags:
                labels.add("\\Draft")
            with self.lock:
                m = super().append(mb.backing.name, raw, flags, internaldate, origin, labels)
                mb.backing.gm_index[m.gm_msgid] = m
                for v in self._views_of(mb.backing):
                    if v.label in m.labels:
                        self._emit(StoreEvent("exists", v.name, [v.view_uid(m)], m.modseq, origin))
                return m
        with self.lock:
            m = super().append(mailbox, raw, flags, internaldate, origin, labels)
            self.get(mailbox).gm_index[m.gm_msgid] = m
            if self.get(mailbox).name == "[Gmail]/All Mail":
                for v in self._views_of(self.get(mailbox)):
                    if v.label in m.labels:
                        self._emit(StoreEvent("exists", v.name, [v.view_uid(m)], m.modseq, origin))
            return m

    def _resolve(self, mailbox, uids) -> list:
        mb = self.get(mailbox)
        return [m for m in (mb.by_uid(u) for u in uids) if m is not None]

    def store_flags(self, mailbox, uids, op, flags, origin=None, per_message=False):
        mb = self.get(mailbox)
        phys = self._physical(mb)
        with self.lock:
            msgs = self._resolve(mailbox, uids)
            changed = super().store_flags(phys.name, [m.uid for m in msgs], op, flags, origin,
                                          per_message=per_message)
            # \Flagged and \Draft are mirrored by the Starred and Drafts views
            for m in changed:
                before = set(m.labels)
                m.labels.discard("\\Starred"); m.labels.discard("\\Draft")
                if "\\Flagged" in m.flags:
                    m.labels.add("\\Starred")
                if "\\Draft" in m.flags:
                    m.labels.add("\\Draft")
                self._label_events(phys, m, before, set(m.labels), m.modseq, origin)
            return changed

    def set_labels(self, mailbox, uids, op, labels, origin=None):
        mb = self.get(mailbox)
        phys = self._physical(mb)
        with self.lock:
            changed = []
            for m in self._resolve(mailbox, uids):
                before = set(m.labels)
                labels = set(labels)
                if op == "add":
                    m.labels |= labels
                elif op == "remove":
                    m.labels -= labels
                else:
                    keep = {mb.label} if isinstance(mb, LabelView) else set()
                    m.labels = set(labels) | keep
                if "\\Starred" in m.labels:
                    m.flags.add("\\Flagged")
                elif "\\Starred" in before:
                    m.flags.discard("\\Flagged")
                m.modseq = phys.next_modseq()
                changed.append(m)
                self._label_events(phys, m, before, set(m.labels), m.modseq, origin)
            if changed and not isinstance(mb, LabelView):
                self._emit(StoreEvent("flags", phys.name, [m.uid for m in changed], phys.highestmodseq, origin))
            return changed

    def expunge(self, mailbox, uids=None, only_deleted=False, origin=None):
        mb = self.get(mailbox)
        with self.lock:
            targets = [m for m in (mb.messages if uids is None else self._resolve(mailbox, uids))
                       if not only_deleted or "\\Deleted" in m.flags]
            if isinstance(mb, LabelView):
                # deleting from a label view only removes the label
                for m in targets:
                    m.flags.discard("\\Deleted")
                self.set_labels(mailbox, [mb.view_uid(m) for m in targets], "remove", [mb.label], origin)
                return targets
            if mb.name == "[Gmail]/All Mail":
                for m in targets:
                    m.flags.discard("\\Deleted")
                self._move_physical(mb, targets, self.get("[Gmail]/Trash"), origin)
                return targets
            gone = super().expunge(mailbox, [m.uid for m in targets], False, origin)
            for m in gone:
                mb.gm_index.pop(m.gm_msgid, None)
            return gone

    def _move_physical(self, src, msgs, dst, origin):
        for m in msgs:
            before = set(m.labels)
            m.labels = set()
            self._label_events(src, m, before, set(), src.next_modseq(), origin)
            src.remove(m)
            src.gm_index.pop(m.gm_msgid, None)
            src.expunged.append((m.uid, src.highestmodseq))
            self._emit(StoreEvent("expunge", src.name, [m.uid], src.highestmodseq, origin))
            n = Message(dst.uidnext, m.raw, m.flags - {"\\Deleted"}, dst.next_modseq(), m.internaldate)
            n.gm_msgid, n.gm_thrid = m.gm_msgid, m.gm_thrid
            dst.uidnext += 1
            dst.add(n)
            dst.gm_index[n.gm_msgid] = n
            self._emit(StoreEvent("exists", dst.name, [n.uid], n.modseq, origin))

    def copy(self, src, uids, dst, origin=None):
        s, d = self.get(src), self.get(dst)
        with self.lock:
            msgs = self._resolve(src, uids)
            pairs = []
            if isinstance(d, LabelView):
                # copying into a label view adds the label (and, from Trash/Spam, restores the message)
                for m in msgs:
                    if self._physical(s) is not d.backing:
                        self._move_physical(self._physical(s), [m], d.backing, origin)
                        m = d.backing.gm_index[m.gm_msgid]
                    self.set_labels(d.backing.name, [m.uid], "add", [d.label], origin)
                    pairs.append((s.view_uid(m) if isinstance(s, LabelView) else m.uid, d.view_uid(m)))
            elif d.name == "[Gmail]/All Mail":
                for m in msgs:
                    if self._physical(s) is not d:
                        self._move_physical(self._physical(s), [m], d, origin)
                        m = d.gm_index[m.gm_msgid]
                    pairs.append((m.uid, m.uid))
            else:  # Trash or Spam: a physical move
                for m in msgs:
                    src_uid = s.view_uid(m) if isinstance(s, LabelView) else m.uid
                    self._move_physical(self._physical(s), [m], d, origin)
                    pairs.append((src_uid, d.gm_index[m.gm_msgid].uid))
            return pairs

    def move(self, src, uids, dst, origin=None):
        with self.lock:
            pairs = self.copy(src, uids, dst, origin)
            s = self.get(src)
            if isinstance(s, LabelView):
                remaining = [u for (u, _) in pairs if s.by_uid(u) is not None]
                if remaining:
                    self.set_labels(src, remaining, "remove", [s.label], origin)
            return pairs

    def truth(self) -> dict:
        """Physical mailboxes only, with labels: what the engine syncs on Gmail."""
        with self.lock:
            return {
                mb.name: {m.uid: {"message_id": m.message_id, "flags": set(m.flags) - {"\\Recent"},
                                  "labels": set(m.labels)} for m in mb.messages}
                for mb in self.mailboxes.values() if not mb.noselect and not isinstance(mb, LabelView)
            }
