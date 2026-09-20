"""
The one place the harness knows the engine's schema. Everything else asserts against the
provider-neutral "placements" view produced here, so when Message.remoteFolderId/remoteUID
become MessageFolder rows only this module changes.

A placement is one physical copy of a message on the server: (folder path, UID) -> the
header Message-ID it carries and its IMAP-visible flags. The same structure is produced from
the server side by harness.servers.base.Server.truth(), which is what makes
`assert local == server` possible without writing expectations by hand.
"""
import json
import sqlite3
from dataclasses import dataclass


@dataclass(frozen=True)
class Placement:
    message_id: str          # engine row id (header hash)
    header_message_id: str   # RFC 5322 Message-ID as stored by the engine
    flags: frozenset         # subset of {"\\Seen", "\\Flagged", "\\Draft", "\\Answered"}
    labels: frozenset = frozenset()   # Gmail X-GM-LABELS the engine recorded, if any


Placements = dict  # folder path -> {uid: Placement}


def _flags_from_message(data: dict) -> frozenset:
    flags = set()
    if not data.get("unread", False):
        flags.add("\\Seen")
    if data.get("starred", False):
        flags.add("\\Flagged")
    if data.get("draft", False):
        flags.add("\\Draft")
    return frozenset(flags)


def has_table(conn: sqlite3.Connection, name: str) -> bool:
    return conn.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)).fetchone() is not None


def folder_paths(conn: sqlite3.Connection) -> dict:
    return {r[0]: r[1] for r in conn.execute("SELECT id, path FROM Folder")}


def placements(conn: sqlite3.Connection) -> Placements:
    paths = folder_paths(conn)
    out: Placements = {p: {} for p in paths.values()}
    if has_table(conn, "MessageFolder"):
        return _placements_from_join_table(conn, paths, out)
    # Pre-placements engines park an unlinked message at remoteUID = UINT32_MAX - phase until
    # the end-of-pass delete (MailProcessor.cpp `remoteUID() > UINT32_MAX - 5`); those rows are
    # not placements.
    rows = conn.execute(
        "SELECT id, headerMessageId, remoteUID, remoteFolderId, remoteXGMLabels, data "
        "FROM Message WHERE remoteUID > 0 AND remoteUID <= 4294967290 "
        "AND remoteFolderId IS NOT NULL AND remoteFolderId != ''"
    ).fetchall()
    for r in rows:
        path = paths.get(r["remoteFolderId"])
        if path is None:
            out.setdefault(f"<unknown folder {r['remoteFolderId']}>", {})
            path = f"<unknown folder {r['remoteFolderId']}>"
        data = json.loads(r["data"])
        labels = frozenset(json.loads(r["remoteXGMLabels"]) if r["remoteXGMLabels"] else [])
        out[path][int(r["remoteUID"])] = Placement(r["id"], r["headerMessageId"] or "", _flags_from_message(data), labels)
    return out


def _placements_from_join_table(conn, paths, out):
    # Post-refactor layout (docs/message-placements-plan.md). Tombstoned rows are not on
    # the server any more and are excluded; UID 0 rows are local-only.
    rows = conn.execute(
        "SELECT mf.messageId, mf.folderId, mf.remoteUID, mf.unread, mf.starred, mf.draft, "
        "mf.remoteXGMLabels, m.headerMessageId FROM MessageFolder mf "
        "JOIN Message m ON m.id = mf.messageId "
        "WHERE mf.remoteUID > 0 AND (mf.unlinkedAt IS NULL OR mf.unlinkedAt = 0)"
    ).fetchall()
    for r in rows:
        path = paths.get(r["folderId"], f"<unknown folder {r['folderId']}>")
        out.setdefault(path, {})
        flags = set()
        if not r["unread"]:
            flags.add("\\Seen")
        if r["starred"]:
            flags.add("\\Flagged")
        if r["draft"]:
            flags.add("\\Draft")
        labels = frozenset(json.loads(r["remoteXGMLabels"]) if r["remoteXGMLabels"] else [])
        out[path][int(r["remoteUID"])] = Placement(r["messageId"], r["headerMessageId"] or "", frozenset(flags), labels)
    return out


def message_count(conn: sqlite3.Connection) -> int:
    return conn.execute("SELECT COUNT(*) FROM Message").fetchone()[0]


def counts_by_folder(conn: sqlite3.Connection) -> dict:
    return {path: len(uids) for path, uids in placements(conn).items()}


def messages(conn: sqlite3.Connection) -> list:
    rows = conn.execute("SELECT id, headerMessageId, subject, unread, starred, draft, threadId, data FROM Message").fetchall()
    out = []
    for r in rows:
        d = dict(r)
        d["data"] = json.loads(d["data"])
        out.append(d)
    return out


def folder_status(conn: sqlite3.Connection) -> dict:
    out = {}
    for r in conn.execute("SELECT path, role, data FROM Folder"):
        out[r["path"]] = {"role": r["role"], **json.loads(r["data"]).get("localStatus", {})}
    return out


def tasks(conn: sqlite3.Connection) -> list:
    return [dict(id=r["id"], status=r["status"], data=json.loads(r["data"])) for r in conn.execute("SELECT id, status, data FROM Task")]


def thread_counts(conn: sqlite3.Connection) -> dict:
    return {r[0]: (r[1], r[2]) for r in conn.execute("SELECT categoryId, unread, total FROM ThreadCounts")}
