"""Comparisons between the engine's database and the server, phrased as lists of
human-readable differences so a failing scenario says exactly what diverged."""
from typing import Iterable, Optional

from .db import Placements
from .servers.base import Truth, normalize_message_id

# The engine models exactly these IMAP flags (unread, starred, draft). \Deleted, \Answered
# and keywords are not stored, so they cannot be compared.
TRACKED_FLAGS = {"\\Seen", "\\Flagged", "\\Draft"}


def compare_placements(local: Placements, truth: Truth, mailboxes: Optional[Iterable[str]] = None,
                       check_flags: bool = True, check_labels: bool = False, ignore_empty: bool = True) -> list:
    diffs = []
    names = set(mailboxes) if mailboxes else set(truth) | {p for p, u in local.items() if u or not ignore_empty}
    for mb in sorted(names):
        remote = truth.get(mb)
        mine = local.get(mb, {})
        if remote is None:
            if mine:
                diffs.append(f"{mb}: engine has {len(mine)} messages in a folder the server does not have")
            continue
        missing = sorted(set(remote) - set(mine))
        extra = sorted(set(mine) - set(remote))
        if missing:
            diffs.append(f"{mb}: {len(missing)} server UIDs missing locally: {_short(missing)}")
        if extra:
            diffs.append(f"{mb}: {len(extra)} local UIDs not on server (ghosts): {_short(extra)}")
        for uid in sorted(set(remote) & set(mine)):
            r, l = remote[uid], mine[uid]
            if normalize_message_id(l.header_message_id) != normalize_message_id(r["message_id"]):
                diffs.append(f"{mb} UID {uid}: Message-ID {l.header_message_id!r} locally vs {r['message_id']!r} on server")
            if check_flags and set(l.flags) & TRACKED_FLAGS != set(r["flags"]) & TRACKED_FLAGS:
                diffs.append(f"{mb} UID {uid}: flags {sorted(set(l.flags) & TRACKED_FLAGS)} locally vs "
                             f"{sorted(set(r['flags']) & TRACKED_FLAGS)} on server")
            if check_labels and set(l.labels) != set(r.get("labels", ())):
                diffs.append(f"{mb} UID {uid}: labels {sorted(l.labels)} locally vs {sorted(r.get('labels', ()))} on server")
    return diffs


def placement_changes(before: Placements, after: Placements) -> list:
    """Which (folder, uid) placements appeared, vanished or changed message between two snapshots."""
    changes = []
    for mb in sorted(set(before) | set(after)):
        b, a = before.get(mb, {}), after.get(mb, {})
        for uid in sorted(set(b) - set(a)):
            changes.append(f"{mb} UID {uid} ({b[uid].header_message_id}) vanished")
        for uid in sorted(set(a) - set(b)):
            changes.append(f"{mb} UID {uid} ({a[uid].header_message_id}) appeared")
        for uid in sorted(set(a) & set(b)):
            if a[uid].message_id != b[uid].message_id:
                changes.append(f"{mb} UID {uid} changed message {b[uid].message_id} -> {a[uid].message_id}")
    return changes


def _short(uids: list, n: int = 8) -> str:
    return ", ".join(str(u) for u in uids[:n]) + (f", … (+{len(uids) - n})" if len(uids) > n else "")
