"""
Scenario-end consistency checks of the engine's derived state against its canonical tables.

The engine maintains several layers incrementally rather than recomputing them:

    MessageFolder rows  ->  Message.data.folders / unread / starred / draft / labels
    Message snapshots   ->  Thread.data.folders / labels (_refs, _u), unread, starred
    Thread arrays       ->  ThreadCategory rows
    ThreadCategory rows ->  ThreadCounts

Each check recomputes one layer from the *stored* layer below it, so a bug surfaces once, at
the layer that diverged, instead of cascading. `check(conn, skip)` returns human-readable
violations; the names in CHECKS are what a scenario lists under `invariants: {skip: [...]}`.
"""
import json
import sqlite3
from collections import defaultdict

from .db import has_table

UNREAD, STARRED, DRAFT = 1, 2, 4
MAX_REPORTED_PER_CHECK = 12


# -- semantics the engine may tighten later; each lives in exactly one place ----------------

def expected_message_flags(rows, drafts_folder_ids: set):
    """unread / starred / draft as MailStore::refreshMessageFromPlacements derives them, or
    None when the message keeps whatever flags it had: OR over its rows, draft also set by a
    copy in a drafts-role folder, flags untouched with no rows (an orphan awaiting the sweep)."""
    if not rows:
        return None
    unread = any(r["unread"] for r in rows)
    starred = any(r["starred"] for r in rows)
    draft = any(r["draft"] or r["folderId"] in drafts_folder_ids for r in rows)
    return {"unread": unread, "starred": starred, "draft": draft}


# -- loading -------------------------------------------------------------------------------

class _Snapshot:
    """Every table the checks read, loaded inside one read transaction."""

    def __init__(self, conn: sqlite3.Connection):
        self.roles = {}         # folder or label id -> role (MailStore::folderById covers both)
        self.paths = {}
        for table in ("Folder", "Label"):
            for r in conn.execute(f"SELECT id, path, role FROM {table}"):
                self.roles[r["id"]] = r["role"] or ""
                self.paths[r["id"]] = r["path"] or ""
        # Same query and order as MailStore::allLabelsCache (findAll<Label>, no ORDER BY).
        self.labels = [dict(r) for r in conn.execute("SELECT id, path, role FROM Label")]

        self.messages = {}
        for r in conn.execute("SELECT id, subject, threadId, unread, starred, draft, data FROM Message"):
            m = dict(r)
            m["data"] = json.loads(m["data"])
            self.messages[m["id"]] = m

        self.rows = defaultdict(list)
        for r in conn.execute("SELECT * FROM MessageFolder ORDER BY rowid"):
            r = dict(r)
            r["labels"] = json.loads(r["remoteXGMLabels"]) if r["remoteXGMLabels"] else []
            self.rows[r["messageId"]].append(r)

        self.orphans = {}       # message id -> since
        if has_table(conn, "MessageOrphan"):
            self.orphans = {r["messageId"]: r["since"] for r in conn.execute("SELECT messageId, since FROM MessageOrphan")}

        self.threads = {}
        for r in conn.execute("SELECT id, subject, data FROM Thread"):
            t = dict(r)
            t["data"] = json.loads(t["data"])
            self.threads[t["id"]] = t

        self.thread_categories = defaultdict(dict)   # thread id -> category id -> row
        for r in conn.execute("SELECT * FROM ThreadCategory"):
            self.thread_categories[r["id"]][r["value"]] = dict(r)

        self.thread_counts = {r["categoryId"]: (r["unread"], r["total"])
                              for r in conn.execute("SELECT categoryId, unread, total FROM ThreadCounts")}

    def folder_name(self, fid: str) -> str:
        return f"{self.paths.get(fid, '<no such folder>')} ({fid})"

    def in_all_mail(self, folder_ids) -> bool:
        """Message::inAllMail: some copy outside spam and trash; unknown folders count."""
        return any(self.roles.get(fid, "") not in ("spam", "trash") for fid in folder_ids)

    def label_for_xgm_name(self, name: str):
        """MailUtils::labelForXGMLabelName: exact path, then `\\Name` against the path
        without `[Gmail]/` or the role (singular or plural), case-insensitively."""
        for label in self.labels:
            if label["path"] == name:
                return label
        if name.startswith("\\"):
            needle = name[1:].lower()
            for label in self.labels:
                path = (label["path"] or "").lower()
                if path.startswith("[gmail]/"):
                    path = path[len("[gmail]/"):]
                role = label["role"] or ""
                if path == needle or role == needle or role == needle + "s":
                    return label
        return None


def _msg(s: _Snapshot, mid: str) -> str:
    m = s.messages.get(mid)
    return f"message {mid} {m['subject']!r}" if m else f"message {mid}"


def _thread(s: _Snapshot, tid: str) -> str:
    t = s.threads.get(tid)
    return f"thread {tid} {t['subject']!r}" if t else f"thread {tid}"


# -- checks --------------------------------------------------------------------------------

def check_message_snapshot(s: _Snapshot) -> list:
    """Message.data.folders == {reported folder: OR of bits} over its rows, and
    Message.data.labels == sorted union of the rows' X-GM-LABELS (when it has rows)."""
    out = []
    for mid, m in s.messages.items():
        rows = s.rows.get(mid, [])
        want = {}
        labels = set()
        for r in rows:
            key = r["pendingFolderId"] or r["folderId"]
            want[key] = want.get(key, 0) | (UNREAD if r["unread"] else 0) | (STARRED if r["starred"] else 0) \
                | (DRAFT if r["draft"] else 0)
            labels.update(r["labels"])
        have = m["data"].get("folders")
        if not isinstance(have, dict) or {k: int(v) for k, v in have.items()} != want:
            out.append(f"{_msg(s, mid)}: folders snapshot {json.dumps(have, sort_keys=True)} != "
                       f"{json.dumps(want, sort_keys=True)} from MessageFolder rows {_rows(rows)}")
        if rows and sorted(m["data"].get("labels") or []) != sorted(labels):
            out.append(f"{_msg(s, mid)}: labels {sorted(m['data'].get('labels') or [])} != "
                       f"{sorted(labels)} from MessageFolder rows {_rows(rows)}")
    return out


def check_message_flags(s: _Snapshot) -> list:
    """Message unread / starred / draft (JSON and indexed columns) against its rows."""
    out = []
    drafts = {fid for fid, role in s.roles.items() if role == "drafts"}
    for mid, m in s.messages.items():
        rows = s.rows.get(mid, [])
        want = expected_message_flags(rows, drafts)
        for flag in ("unread", "starred", "draft"):
            data_value = bool(m["data"].get(flag, False))
            if bool(m[flag]) != data_value:
                out.append(f"{_msg(s, mid)}: column {flag}={bool(m[flag])} but data.{flag}={data_value}")
            if want is not None and data_value != want[flag]:
                out.append(f"{_msg(s, mid)}: {flag}={data_value}, expected {want[flag]} from MessageFolder rows {_rows(rows)}")
    return out


def check_thread_refcounts(s: _Snapshot) -> list:
    """Thread folder / label refcounts and counters against its messages' snapshots
    (Thread::applyMessageAttributeChanges summed over every message of the thread)."""
    out = []
    by_thread = defaultdict(list)
    for mid, m in s.messages.items():
        tid = m["data"].get("threadId") or ""
        if tid:
            by_thread[tid].append(m)
            if tid not in s.threads:
                out.append(f"{_msg(s, mid)}: threadId {tid} names no Thread row")

    for tid, t in s.threads.items():
        msgs = by_thread.get(tid, [])
        folders = defaultdict(lambda: [0, 0])
        labels = defaultdict(lambda: [0, 0])
        unread = starred = 0
        for m in msgs:
            d = m["data"]
            unread += bool(d.get("unread"))
            starred += bool(d.get("starred"))
            snapshot = d.get("folders") or {}
            for fid, bits in snapshot.items():
                folders[fid][0] += 1
                folders[fid][1] += 1 if int(bits) & UNREAD else 0
            label_unread = 1 if d.get("unread") and s.in_all_mail(snapshot) else 0
            for name in d.get("labels") or []:
                label = s.label_for_xgm_name(name)
                if label is not None:
                    labels[label["id"]][0] += 1
                    labels[label["id"]][1] += label_unread

        where = f"{_thread(s, tid)} ({len(msgs)} messages)"
        for kind, want, have_list in (("folder", folders, t["data"].get("folders") or []),
                                      ("label", labels, t["data"].get("labels") or [])):
            have = {}
            for entry in have_list:
                if entry["id"] in have:
                    out.append(f"{where}: {kind} {s.folder_name(entry['id'])} listed twice")
                have[entry["id"]] = [entry.get("_refs"), entry.get("_u")]
            for cid in sorted(set(want) | set(have)):
                w, h = want.get(cid), have.get(cid)
                if w != h:
                    out.append(f"{where}: {kind} {s.folder_name(cid)} [_refs, _u] = {h}, expected {w} "
                               f"from messages {_thread_messages(msgs)}")
        if t["data"].get("unread") != unread or t["data"].get("starred") != starred:
            out.append(f"{where}: unread/starred = {t['data'].get('unread')}/{t['data'].get('starred')}, "
                       f"expected {unread}/{starred} from messages {_thread_messages(msgs)}")
        have_folders = t["data"].get("folders") or []
        in_all_mail = len(have_folders) > sum(1 for f in have_folders if f.get("role") in ("spam", "trash"))
        if t["data"].get("inAllMail") != in_all_mail:
            out.append(f"{where}: inAllMail = {t['data'].get('inAllMail')}, expected {in_all_mail} "
                       f"from its folders {[f.get('role') or f.get('path') for f in have_folders]}")
    return out


def check_thread_categories(s: _Snapshot) -> list:
    """ThreadCategory rows == the thread's folder + label ids, unread = (_u > 0), carrying the
    thread's inAllMail and timestamps (Thread::afterSave)."""
    out = []
    for tid in sorted(set(s.threads) | set(s.thread_categories)):
        t = s.threads.get(tid)
        have = s.thread_categories.get(tid, {})
        if t is None:
            out.append(f"ThreadCategory has rows {sorted(have)} for missing thread {tid}")
            continue
        d = t["data"]
        want = {e["id"]: e.get("_u", 0) > 0 for e in (d.get("folders") or []) + (d.get("labels") or [])}
        if set(want) != set(have):
            out.append(f"{_thread(s, tid)}: ThreadCategory values {sorted(have)}, expected {sorted(want)}")
        for cid in sorted(set(want) & set(have)):
            row = have[cid]
            if bool(row["unread"]) != want[cid]:
                out.append(f"{_thread(s, tid)}: ThreadCategory {s.folder_name(cid)} unread={bool(row['unread'])}, "
                           f"expected {want[cid]}")
            if bool(row["inAllMail"]) != bool(d.get("inAllMail")):
                out.append(f"{_thread(s, tid)}: ThreadCategory {s.folder_name(cid)} inAllMail={bool(row['inAllMail'])}, "
                           f"thread says {d.get('inAllMail')}")
            for col, key in (("lastMessageReceivedTimestamp", "lmrt"), ("lastMessageSentTimestamp", "lmst")):
                if float(row[col] or 0) != float(d.get(key) or 0):
                    out.append(f"{_thread(s, tid)}: ThreadCategory {s.folder_name(cid)} {col}={row[col]}, "
                               f"thread {key}={d.get(key)}")
    return out


def check_thread_counts(s: _Snapshot) -> list:
    """ThreadCounts (unread, total) == counts of ThreadCategory rows per category. Only
    categories with a ThreadCounts row are counted: Thread::afterSave UPDATEs, never inserts."""
    out = []
    totals = defaultdict(lambda: [0, 0])
    for cats in s.thread_categories.values():
        for cid, row in cats.items():
            totals[cid][0] += 1 if row["unread"] else 0
            totals[cid][1] += 1
    for cid, (unread, total) in sorted(s.thread_counts.items()):
        want = tuple(totals.get(cid, (0, 0)))
        if (unread, total) != want:
            out.append(f"ThreadCounts {s.folder_name(cid)}: (unread, total) = {(unread, total)}, "
                       f"expected {want} from ThreadCategory")
    return out


def check_orphans(s: _Snapshot) -> list:
    """MessageOrphan lists exactly the messages with no MessageFolder row: the end-of-pass
    sweep finds orphans only through it, so a missing record is a message that lives forever."""
    out = [f"{_msg(s, mid)}: no MessageFolder rows and no MessageOrphan record "
           f"(folders={json.dumps(m['data'].get('folders'))}, threadId={m['data'].get('threadId')})"
           for mid, m in s.messages.items() if not s.rows.get(mid) and mid not in s.orphans]
    out += [f"MessageOrphan record for {_msg(s, mid)} (since {since}), which "
            + (f"has MessageFolder rows {_rows(s.rows[mid])}" if s.rows.get(mid) else "does not exist")
            for mid, since in s.orphans.items() if s.rows.get(mid) or mid not in s.messages]
    return out


CHECKS = {
    "message_snapshot": check_message_snapshot,
    "message_flags": check_message_flags,
    "thread_refcounts": check_thread_refcounts,
    "thread_categories": check_thread_categories,
    "thread_counts": check_thread_counts,
    "orphans": check_orphans,
}


def check(conn: sqlite3.Connection, skip=()) -> list:
    unknown = set(skip) - set(CHECKS)
    if unknown:
        raise ValueError(f"unknown invariants {sorted(unknown)}; known: {sorted(CHECKS)}")
    if not has_table(conn, "MessageFolder"):
        return []   # a pre-placements database: nothing here applies
    conn.execute("BEGIN")   # one snapshot across every table, even if the engine commits meanwhile
    try:
        s = _Snapshot(conn)
    finally:
        conn.execute("ROLLBACK")
    out = []
    for name, fn in CHECKS.items():
        if name in skip:
            continue
        found = fn(s)
        out += [f"[{name}] {v}" for v in found[:MAX_REPORTED_PER_CHECK]]
        if len(found) > MAX_REPORTED_PER_CHECK:
            out.append(f"[{name}] ... and {len(found) - MAX_REPORTED_PER_CHECK} more")
    return out


def _rows(rows) -> str:
    return "[" + ", ".join(
        f"{{folder={r['folderId']} uid={r['remoteUID']} u={r['unread']} s={r['starred']} d={r['draft']}"
        + (f" pending={r['pendingFolderId']}" if r["pendingFolderId"] else "")
        + (f" labels={r['labels']}" if r["labels"] else "") + "}"
        for r in rows) + "]"


def _thread_messages(msgs) -> str:
    return "[" + ", ".join(
        f"{m['id'][:12]} u={bool(m['data'].get('unread'))} folders={json.dumps(m['data'].get('folders'), sort_keys=True)}"
        + (f" labels={m['data'].get('labels')}" if m["data"].get("labels") else "")
        for m in msgs[:6]) + (", ..." if len(msgs) > 6 else "") + "]"
