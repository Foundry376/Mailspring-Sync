"""
Scenario files describe a server state, a sequence of things that happen while mailsync
runs against it, and what must be true afterwards. See test/README.md for the vocabulary.

    name: qresync-bulk-expunge-during-idle
    source: Mailspring-Sync PR #141
    servers: [{fake: dovecot}, {dovecot: qresync}]
    mailboxes:
      INBOX: {messages: 200}
    steps:
      - wait: quiescent
      - server.expunge: {mailbox: INBOX, uids: "140:195"}
      - sync: pass
    expect:
      db_matches_server: {}
      stable: {passes: 1}
"""
import json
import os
import shutil
import socket
import sqlite3
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import yaml

from . import db as dbmod
from . import invariants
from . import mailgen
from .assertions import TRACKED_FLAGS, compare_placements, placement_changes
from .mailsync import MailsyncError, MailsyncProcess, account_json, describe_exit
from .servers.base import Server, normalize_message_id

TEST_DIR = Path(__file__).resolve().parents[1]
# Each process gets its own artifacts directory so concurrent sessions (two agents, or ab.py
# recordings of two binaries) never delete each other's runs. HARNESS_RUNS_DIR overrides it.
RUNS_DIR = Path(os.environ.get("HARNESS_RUNS_DIR") or TEST_DIR / "runs" / f"session-{os.getpid()}")


class ScenarioFailure(AssertionError):
    pass


class ScenarioSkipped(Exception):
    pass


SERVER_KINDS = ("fake", "dovecot", "cyrus")


@dataclass
class ServerSpec:
    kind: str          # fake | dovecot | cyrus
    profile: str       # personality name, or the dovecot / cyrus profile name
    options: dict = field(default_factory=dict)

    @classmethod
    def parse(cls, entry) -> "ServerSpec":
        if isinstance(entry, str):
            kind, _, profile = entry.partition(":")
            return cls(kind, profile or "dovecot")
        (kind, profile), = [(k, v) for k, v in entry.items() if k in SERVER_KINDS]
        options = {k: v for k, v in entry.items() if k not in SERVER_KINDS}
        return cls(kind, profile, options)

    @property
    def id(self) -> str:
        return f"{self.kind}:{self.profile}"


def load_scenario(path: Path) -> dict:
    with open(path) as f:
        sc = yaml.safe_load(f)
    sc.setdefault("name", path.stem)
    sc["_path"] = str(path)
    sc.setdefault("servers", [{"fake": "dovecot"}])
    return sc


def make_server(spec: ServerSpec, log_path: Optional[str]) -> Server:
    if spec.kind == "fake":
        from .servers.fake import FakeServer
        opts = {k: v for k, v in spec.options.items() if k != "imap_host"}
        return FakeServer(spec.profile, log_path=log_path, **opts)
    # `smtp: true` on a real server is attached after it starts (ScenarioRun.setup).
    opts = {k: v for k, v in spec.options.items() if k != "smtp"}
    if spec.kind == "dovecot":
        from .servers.dovecot import DovecotServer
        return DovecotServer(spec.profile, **opts)
    if spec.kind == "cyrus":
        from .servers.cyrus import CyrusServer
        return CyrusServer(spec.profile, log_path=log_path, **opts)
    raise ValueError(f"unknown server kind {spec.kind}")


def parse_uids(spec) -> list:
    if isinstance(spec, int):
        return [spec]
    if isinstance(spec, list):
        out = []
        for s in spec:
            out += parse_uids(s)
        return out
    out = []
    for part in str(spec).split(","):
        if ":" in part:
            a, b = part.split(":")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


class ScenarioRun:
    """One scenario against one server. Artifacts land in test/runs/<name>-<server>/."""

    def __init__(self, scenario: dict, spec: ServerSpec, binary: Optional[Path] = None, keep: bool = False):
        self.sc = scenario
        self.spec = spec
        self.binary = binary            # the build under test (--mailsync / MAILSYNC_BIN / auto)
        self.keep = keep or bool(os.environ.get("HARNESS_KEEP"))
        self.name = f"{scenario['name']}-{spec.kind}-{spec.profile}".replace("/", "_")
        self.work = RUNS_DIR / self.name
        self.server: Optional[Server] = None
        self.ms: Optional[MailsyncProcess] = None
        self.earlier_deltas: list = []  # deltas from engine processes a `restart` stopped
        self.msg_counter = 1
        self.labels: dict = {}          # step labels -> values (task ids, snapshots)
        self.snapshots: dict = {}
        self.ignore_busy = (scenario.get("quiescence") or {}).get("ignore_busy", [])
        self.report: list = []          # human-readable trail of what happened
        self.failures: list = []
        self.xfails: list = []          # expectations that failed and were expected to (known engine bugs)
        self.xpasses: list = []         # expectations marked xfail that passed: remove the marker

    # -- setup --------------------------------------------------------------------------------

    def _note(self, msg: str):
        self.report.append(f"[{self.ms.elapsed() if self.ms else 0:6.1f}s] {msg}")

    def _next_messages(self, spec) -> list:
        """Build messages from a count or a dict of mailgen options; ids never repeat within a run."""
        if isinstance(spec, int):
            spec = {"count": spec}
        count = int(spec.get("count", 1))
        kw = {k: v for k, v in spec.items() if k not in ("count", "self_addressed", "thread")}
        if "attachment" in kw:
            kw["attachment"] = tuple(kw["attachment"])
        out = []
        for _ in range(count):
            n = self.msg_counter
            if spec.get("thread"):
                msgs = mailgen.thread(n, int(spec["thread"]), **kw)
                self.msg_counter += 1
            elif spec.get("self_addressed"):
                msgs = [mailgen.self_addressed(n, **kw)]
                self.msg_counter += 1
            else:
                msgs = [mailgen.message(n, **kw)]
                self.msg_counter += 1
            out += msgs
        return out

    def _check_host_alias(self):
        host = self.spec.options.get("imap_host")
        if not host or host in ("127.0.0.1", "localhost"):
            return
        try:
            resolved = socket.gethostbyname(host)
        except socket.gaierror:
            resolved = None
        if resolved != "127.0.0.1":
            raise ScenarioSkipped(
                f"{self.sc['name']} needs {host} to resolve to 127.0.0.1 (the engine gates this "
                f"behaviour on the hostname). Add `127.0.0.1 {host}` to /etc/hosts to enable it."
            )

    def _initial_binary(self) -> Optional[Path]:
        """A scenario may start on another build (`binary: ab/mailsync-0df7864`, relative to
        test/) and `restart` onto the build under test, which is how a database written by
        an older engine is handed to the current one."""
        if not self.sc.get("binary"):
            return self.binary
        path = self._binary_path(self.sc["binary"])
        if not path.is_file():
            raise ScenarioSkipped(
                f"{self.sc['name']} starts on {self.sc['binary']}, which is not at {path}. "
                f"Build it first (docs/handoff-refactor-regression.md 4 for a baseline build)."
            )
        return path

    @staticmethod
    def _binary_path(value: str) -> Path:
        path = Path(value).expanduser()
        return path if path.is_absolute() else TEST_DIR / path

    def setup(self):
        self._check_host_alias()
        if self.work.exists():
            shutil.rmtree(self.work)
        self.work.mkdir(parents=True)
        self.server = make_server(self.spec, str(self.work / "server.log"))
        self.server.start()
        if self.spec.kind != "fake" and self.spec.options.get("smtp"):
            self.server.attach_smtp()
        self.ignore_busy = [self.server.server_path(p) for p in self.ignore_busy]
        existing = set(self.server.mailboxes())
        for name, mspec in (self.sc.get("mailboxes") or {}).items():
            mspec = mspec or {}
            if name not in existing:
                self.server.create_mailbox(name, mspec.get("special_use"))
            if mspec.get("messages"):
                flags = mspec.get("flags", ["\\Seen"])
                self.server.populate(name, self._next_messages(mspec["messages"]), flags)
            if mspec.get("duplicate_of"):
                d = mspec["duplicate_of"]
                self.server.duplicate(d["mailbox"], parse_uids(d["uids"]), name)
        for step in self.sc.get("setup") or []:
            self.run_step(step, allow_client=False)
        kw = self.server.account_kwargs()
        if self.spec.options.get("imap_host"):
            kw["imap_host"] = self.spec.options["imap_host"]
        if self.sc.get("account"):
            kw.update(self.sc["account"])
        account = account_json(**kw)
        self.ms = MailsyncProcess(account, self.work, binary=self._initial_binary(), verbose=True, env=self._engine_env())

    def _engine_env(self) -> dict:
        """A scenario's top-level `env:` is added to the engine's environment, for the knobs the
        engine reads from it (e.g. ORPHAN_SWEEP_MAX_WAIT)."""
        return {k: str(v) for k, v in (self.sc.get("env") or {}).items()}

    def teardown(self):
        if self.ms:
            code = self.ms.stop()
            self._note(f"mailsync stopped ({describe_exit(code)})")
        if self.server:
            self.server.stop()
            if hasattr(self.server, "detach_smtp"):
                self.server.detach_smtp()
        if self.work.exists():
            (self.work / "report.txt").write_text("\n".join(self.report) + "\n")
        if not self.keep and not self.failures and not self.xfails and self.work.exists():
            shutil.rmtree(self.work, ignore_errors=True)
            try:
                self.work.parent.rmdir()   # the session dir, once its last run is gone
            except OSError:
                pass

    # -- steps --------------------------------------------------------------------------------

    def run(self) -> list:
        """Runs the whole scenario; returns the list of failures (empty = pass)."""
        started = False
        try:
            self.setup()
            self.ms.start()
            started = True
            self._note(f"mailsync {self.ms.binary.resolve().name} started against {self.spec.id} on port {self.server.port}; migrate said: {self.ms.migrate_output}")
            for step in self.sc.get("steps") or []:
                self.run_step(step)
            self.check_expectations(self._final_expectations())
        except ScenarioSkipped:
            raise
        except ScenarioFailure as e:
            self.failures.append(str(e))
        except MailsyncError as e:
            if not started:
                raise   # could not even launch: a harness/binary problem, not an engine verdict
            # The process died mid-scenario. Still evaluate the expectations so that a
            # scenario whose `running`/`db_matches_server` are xfail'd for a known crash is
            # reported as xfail rather than as an error.
            self._note(f"mailsync died: {e}")
            try:
                self.check_expectations(self._final_expectations())
            except ScenarioFailure as e2:
                self.failures.append(str(e2))
            except MailsyncError as e2:
                self.failures.append(f"mailsync died: {e2}\n{self.ms.stderr_tail() if self.ms else ''}")
        except Exception as e:
            self._note(f"{type(e).__name__}: {e}")
            if self.ms:
                self._note(self.ms.describe())
            self.failures.append(f"{type(e).__name__}: {e}")
        finally:
            self.teardown()
        return self.failures

    def run_step(self, step, allow_client: bool = True):
        if isinstance(step, str):
            step = {step: {}}
        (verb, arg), = [(k, v) for k, v in step.items() if k not in ("as", "note")]
        label = step.get("as")
        self._note(f"step {verb} {json.dumps(arg) if not isinstance(arg, str) else arg}")
        result = None
        if verb == "wait":
            result = self._wait(arg)
        elif verb == "sync":
            result = self.ms.sync_pass(timeout=float(arg.get("timeout", 180)) if isinstance(arg, dict) else 180,
                                       ignore_busy=self.ignore_busy)
        elif verb == "snapshot":
            result = self._snapshot(arg if isinstance(arg, str) else label or "snapshot")
        elif verb == "restart":
            result = self._restart(arg or {})
        elif verb == "force_scans":
            result = self._force_scans(arg or {})
        elif verb == "assert":
            self.check_expectations(arg)
        elif verb.startswith("server."):
            result = self._server_step(verb[len("server."):], arg or {})
        elif verb.startswith("client."):
            if not allow_client:
                raise ScenarioFailure(f"{verb} is not allowed in setup (mailsync is not running yet)")
            result = self._client_step(verb[len("client."):], arg or {})
        else:
            raise ScenarioFailure(f"unknown step {verb!r}")
        if label:
            self.labels[label] = result
        return result

    def _wait(self, arg):
        if arg == "quiescent" or arg is None:
            self.ms.wait_quiescent(timeout=180, ignore_busy=self.ignore_busy)
            return
        if isinstance(arg, (int, float)):
            time.sleep(float(arg))
            return
        if "seconds" in arg:
            time.sleep(float(arg["seconds"]))
        if "quiescent" in arg:
            self.ms.wait_quiescent(timeout=float(arg.get("timeout", 180)), settle=float(arg.get("settle", 2.0)),
                                   ignore_busy=self.ignore_busy)
        if "log" in arg:
            self.ms.wait_for_log(arg["log"], timeout=float(arg.get("timeout", 60)))
        if "counts" in arg:
            self.ms.wait_for(lambda: not self.expect_counts(arg["counts"]), float(arg.get("timeout", 60)),
                             f"placement counts {arg['counts']}")
        if "task" in arg:
            task_id = self.labels[arg["task"]]
            self._wait_task(task_id, float(arg.get("timeout", 60)))
        if "idle" in arg and self.spec.kind == "fake":
            self.server.imap.wait_for_idle(float(arg.get("timeout", 60)))

    def _wait_task(self, task_id: str, timeout: float):
        def done():
            with self.ms.db() as c:
                row = c.execute("SELECT status FROM Task WHERE id = ?", (task_id,)).fetchone()
            return row is not None and row[0] in ("complete", "cancelled")
        self.ms.wait_for(done, timeout, f"task {task_id} to complete")
        with self.ms.db() as c:
            row = c.execute("SELECT data FROM Task WHERE id = ?", (task_id,)).fetchone()
        data = json.loads(row[0])
        if data.get("error"):
            self._note(f"task {task_id} finished with error {json.dumps(data['error'])}")

    def _named(self, by_path: dict) -> dict:
        """Re-key a {Folder.path: ...} map from the engine's database by scenario mailbox name."""
        return {self.server.scenario_name(path): v for path, v in by_path.items()}

    def _snapshot(self, name: str):
        with self.ms.db() as c:
            snap = self._named(dbmod.placements(c))
        self.snapshots[name] = snap
        return snap

    def _restart(self, arg: dict):
        code = self.ms.stop()
        self.earlier_deltas += self.ms.deltas()
        self._note(f"mailsync stopped for restart (exit {code})")
        # Steps in `before:` run while the engine is down - the honest way to build up server
        # state the engine did not watch happen (e.g. thousands of flag changes that make one
        # large modseq gap, as when the app was closed for a long time).
        for step in arg.get("before") or []:
            self.run_step(step, allow_client=False)
        account = self.ms.account
        # Without `binary:` the restart lands on the build under test, whatever the scenario
        # started on. Relative paths resolve against test/, like the top-level `binary:`.
        binary = self._binary_path(arg["binary"]) if arg.get("binary") else self.binary
        self.ms = MailsyncProcess(account, self.work, binary=binary, verbose=True, env=self._engine_env())
        self.ms.start()
        self._note(f"mailsync {self.ms.binary.resolve().name} restarted; migrate said: {self.ms.migrate_output}")
        if arg.get("wait", True):
            self.ms.wait_quiescent(timeout=float(arg.get("timeout", 180)), ignore_busy=self.ignore_busy)

    def _force_scans(self, arg: dict):
        """Stopgap until the engine reads scan intervals from the environment: backdate the
        folders' last deep/shallow scan so the next pass takes the scan branch, then wake."""
        keys = arg.get("keys", ["lastDeep", "lastShallow", "lastCleanup"])
        conn = sqlite3.connect(str(self.ms.db_path), timeout=30)
        try:
            for key in keys:
                conn.execute(f"UPDATE Folder SET data = json_set(data, '$.localStatus.{key}', 0)")
            conn.commit()
        finally:
            conn.close()
        self._note(f"backdated {keys} on every folder")
        if arg.get("sync", True):
            self.ms.sync_pass(timeout=float(arg.get("timeout", 180)), ignore_busy=self.ignore_busy)

    def _resolve_messages(self, sel: dict) -> list:
        """{mailbox, uids} -> engine message ids, via the placements view; or
        {header_message_ids: [...]} for rows that have no server placement yet (a draft the
        client saved locally sits at UID 0 and is invisible to the placements view)."""
        if "header_message_ids" in sel:
            wanted = [h.strip("<>") for h in sel["header_message_ids"]]
            with self.ms.db() as c:
                rows = c.execute("SELECT id, headerMessageId FROM Message").fetchall()
            by_hmid = {(r["headerMessageId"] or "").strip("<>"): r["id"] for r in rows}
            missing = [h for h in wanted if h not in by_hmid]
            if missing:
                raise ScenarioFailure(f"messages with Message-ID {missing} are not in the engine's database")
            return [by_hmid[h] for h in wanted]
        with self.ms.db() as c:
            pl = self._named(dbmod.placements(c))
        folder = pl.get(sel["mailbox"], {})
        uids = parse_uids(sel["uids"]) if "uids" in sel else sorted(folder)
        missing = [u for u in uids if u not in folder]
        if missing:
            raise ScenarioFailure(f"messages {sel['mailbox']} UIDs {missing} are not in the engine's database")
        return [folder[u].message_id for u in uids]

    def _resolve_threads(self, sel: dict) -> list:
        ids = self._resolve_messages(sel)
        with self.ms.db() as c:
            rows = c.execute(f"SELECT DISTINCT threadId FROM Message WHERE id IN ({','.join('?' * len(ids))})", ids).fetchall()
        return [r[0] for r in rows if r[0]]

    def _folder_json(self, path: str) -> dict:
        """Folder (or, on Gmail, Label) JSON as the client would pass it in a task."""
        with self.ms.db() as c:
            path = self.server.server_path(path)
            row = c.execute("SELECT data FROM Folder WHERE path = ?", (path,)).fetchone()
            if row is None:
                row = c.execute("SELECT data FROM Label WHERE path = ?", (path,)).fetchone()
        if row is None:
            raise ScenarioFailure(f"folder/label {path!r} is not in the engine's database")
        return json.loads(row[0])

    def _server_step(self, op: str, arg: dict):
        at = arg.pop("at", None)
        if at and self.spec.kind == "fake":
            # Defer until the protocol moment; the fake runs the hook on the session's thread.
            # `at: name` fires on whichever session gets there first; `at: {hook, session,
            # command, mailbox}` narrows it (e.g. the background worker's next UID FETCH of
            # INBOX while the foreground idles).
            spec = {"hook": at} if isinstance(at, str) else dict(at)
            hook = spec.pop("hook")
            # `delay` holds the hooked session's thread after the change, i.e. the server
            # answers that session's command late - long enough for another connection to
            # act on the change first.
            delay = float(spec.pop("delay", 0))
            # `every: true` keeps the hook armed, e.g. to fail a folder's STATUS on every pass.
            once = not spec.pop("every", False)

            def fire(session):
                self._note(f"hook {hook} {spec or ''} fired on session {session.sid} "
                           f"({'foreground' if session.has_idled else 'background'}, {session.selected} selected): "
                           f"server.{op} {json.dumps(arg)}")
                if op == "reject":
                    session.reject_next = (arg.get("code"), arg.get("text", "Command failed."))
                else:
                    self._server_op(op, arg)
                if delay:
                    time.sleep(delay)
            self.server.at(hook, fire, once=once, **spec)
            return
        if at:
            self._note(f"(server {self.spec.kind} has no hook {at}; applying server.{op} immediately)")
        return self._server_op(op, arg)

    def _server_op(self, op: str, arg: dict):
        s = self.server
        if op == "expunge":
            s.expunge(arg["mailbox"], parse_uids(arg["uids"]))
        elif op == "flags":
            s.set_flags(arg["mailbox"], parse_uids(arg["uids"]), add=arg.get("add", []), remove=arg.get("remove", []),
                        per_message=bool(arg.get("per_message", False)))
        elif op == "labels":
            s.set_labels(arg["mailbox"], parse_uids(arg["uids"]), add=arg.get("add", []), remove=arg.get("remove", []))
        elif op == "move":
            s.move(arg["mailbox"], parse_uids(arg["uids"]), arg["to"])
        elif op == "copy":
            s.copy(arg["mailbox"], parse_uids(arg["uids"]), arg["to"])
        elif op == "duplicate":
            s.duplicate(arg["mailbox"], parse_uids(arg["uids"]), arg["to"])
        elif op == "append":
            raws = self._next_messages(arg.get("messages", 1))
            return s.populate(arg["mailbox"], raws, arg.get("flags", ["\\Seen"]))
        elif op == "create_mailbox":
            s.create_mailbox(arg["name"], arg.get("special_use"))
        elif op == "delete_mailbox":
            s.delete_mailbox(arg["name"])
        elif op == "set_uidvalidity":
            s.set_uidvalidity(arg["mailbox"], int(arg["value"]))
        elif op == "set_uidnext":
            s.set_uidnext(arg["mailbox"], int(arg["value"]))
        elif op == "drop_connections":
            s.drop_connections()
        elif op == "pause":
            pass  # with `at` + `delay`: holds one reply without changing the server
        elif op == "reject":
            pass  # only meaningful with `at` on the fake, which answers the hooked command NO
        elif op == "smtp_hold":
            self._smtp().hold()
        elif op == "smtp_release":
            self._note(f"released {self._smtp().release()} held SMTP deliveries")
        else:
            raise ScenarioFailure(f"unknown server op {op!r}")

    def _smtp(self):
        smtp = getattr(self.server, "smtp", None)
        if smtp is None:
            raise ScenarioFailure("this step needs SMTP: add `smtp: true` to the server entry")
        return smtp

    def _client_step(self, op: str, arg: dict):
        if op == "wake":
            self.ms.wake()
            return
        if op == "task":
            task = dict(arg)
            if "messages" in task:
                task["messageIds"] = self._resolve_messages(task.pop("messages"))
            if "threads" in task:   # {mailbox, uids} -> the distinct threadIds of those messages
                task["threadIds"] = self._resolve_threads(task.pop("threads"))
            if task.get("__cls") == "DestroyCategoryTask":   # the engine DELETEs this path
                task["path"] = self._folder_json(task["path"])["path"]
            if isinstance(task.get("folder"), str):
                task["folder"] = self._folder_json(task["folder"])
            if "sourceFolders" in task:   # the perspective's folders, as paths -> sourceFolderIds
                task["sourceFolderIds"] = [self._folder_json(p)["id"] for p in task.pop("sourceFolders")]
            for key in ("labelsToAdd", "labelsToRemove"):
                if key in task:
                    task[key] = [self._folder_json(l) if isinstance(l, str) else l for l in task[key]]
            if isinstance(task.get("draft"), dict):
                # the client serializes these on every Message; the engine reads them unguarded
                draft = task["draft"]
                draft.setdefault("aid", self.ms.account["id"])
                draft.setdefault("v", 0)
                draft.setdefault("date", int(time.time()))
                draft.setdefault("__cls", "Message")
            task_id = self.ms.queue_task(task)
            self._note(f"queued {task['__cls']} as {task_id}")
            return task_id
        if op == "undo_task":
            return self._undo_task(arg)
        if op == "need_bodies":
            self.ms.need_bodies(self._resolve_messages(arg["messages"]))
            return
        raise ScenarioFailure(f"unknown client op {op!r}")

    def _undo_task(self, arg: dict):
        """{of: label}: queue the undo of a completed task the way UndoRedoStore does
        (Task.createIdenticalTask + ChangeFolderTask.createUndoTasks): same class and item
        ids, isUndo, and for a ChangeFolderTask the engine-written `undoPlacements` copied to
        `restorePlacements`, `sourceFolderIds` set to the original destination and `folder`
        to the first recorded source folder."""
        original_id = self.labels[arg["of"]]
        with self.ms.db() as c:
            row = c.execute("SELECT data FROM Task WHERE id = ?", (original_id,)).fetchone()
        if row is None:
            raise ScenarioFailure(f"task {original_id} is not in the engine's database")
        data = json.loads(row[0])
        undo = {k: v for k, v in data.items() if k not in ("id", "status", "v", "error", "undoPlacements", "createdAt")}
        undo["isUndo"] = True
        if data.get("__cls") == "ChangeFolderTask":
            placements = data.get("undoPlacements") or {}
            if not placements:
                raise ScenarioFailure(f"task {original_id} recorded no undoPlacements; the engine's local phase did not run")
            undo["restorePlacements"] = placements
            undo["sourceFolderIds"] = [data["folder"]["id"]]
            first = next((entry["folderId"] for entries in placements.values() for entry in entries), None)
            with self.ms.db() as c:
                frow = c.execute("SELECT data FROM Folder WHERE id = ?", (first,)).fetchone()
            if frow is not None:
                undo["folder"] = json.loads(frow[0])
        elif "unread" in undo:
            undo["unread"] = not undo["unread"]
        elif "starred" in undo:
            undo["starred"] = not undo["starred"]
        task_id = self.ms.queue_task(undo)
        self._note(f"queued undo of {original_id} ({undo['__cls']}) as {task_id}: restorePlacements={json.dumps(undo.get('restorePlacements'))}")
        return task_id

    # -- expectations ---------------------------------------------------------------------------

    def _final_expectations(self) -> dict:
        """The scenario's `expect`, with `invariants` appended last unless it is `false`, so
        every scenario ends with the derived-state consistency check."""
        expect = dict(self.sc.get("expect") or {})
        inv = expect.pop("invariants", {})
        if inv is not False:
            expect["invariants"] = inv
        return expect

    def check_expectations(self, expect: dict):
        problems = []
        crash_expected = False
        for key, arg in expect.items():
            arg = dict(arg) if isinstance(arg, dict) else (arg or {})
            xfail = self._xfail_reason(arg.pop("xfail", None)) if isinstance(arg, dict) else None
            if key == "running" and xfail:
                crash_expected = True
            checker = getattr(self, f"expect_{key}", None)
            if checker is None:
                raise ScenarioFailure(f"unknown expectation {key!r}")
            try:
                found = checker(arg)
            except MailsyncError as e:
                found = [f"{key}: {e}"]   # the process died while checking; still subject to xfail
            if xfail:
                # A known engine bug: failing is expected and recorded, passing means the
                # marker can be removed. Neither fails the scenario.
                if found:
                    self.xfails.append(f"{key}: {xfail}")
                    self._note(f"XFAIL {key} ({xfail}): " + "; ".join(found[:3]))
                else:
                    self.xpasses.append(f"{key}: {xfail}")
                    self._note(f"XPASS {key} - passes now, remove xfail: {xfail}")
                continue
            problems += found
        if self.ms and self.ms.exit_code is not None and not expect.get("exit") and not crash_expected:
            problems.append(f"mailsync stopped ({describe_exit(self.ms.exit_code)})\n{self.ms.stderr_tail()}")
        for p in problems:
            self._note("FAIL " + p)
        if problems:
            raise ScenarioFailure("\n".join(problems))

    def _xfail_reason(self, xfail):
        """`xfail: reason` applies to every server; `xfail: {dovecot: reason, "fake:plain": reason}`
        only to the matching server kind or kind:profile."""
        if not xfail or isinstance(xfail, str):
            return xfail
        for key in (self.spec.id, self.spec.kind):
            if key in xfail:
                return xfail[key]
        return None

    def expect_db_matches_server(self, arg: dict) -> list:
        with self.ms.db() as c:
            local = self._named(dbmod.placements(c))
        truth = self.server.truth()
        return compare_placements(local, truth, mailboxes=arg.get("mailboxes"),
                                  check_flags=arg.get("flags", True), check_labels=arg.get("labels", False))

    def expect_invariants(self, arg: dict) -> list:
        """Derived state (message snapshots, thread refcounts, ThreadCategory, ThreadCounts)
        recomputed from the canonical tables; see harness/invariants.py. `skip: [names]`
        disables individual checks. Several derived layers are written in separate
        transactions, so the check waits for quiescence first."""
        if self.ms.running:
            try:
                self.ms.wait_quiescent(timeout=60, ignore_busy=self.ignore_busy)
            except TimeoutError:
                self._note(f"invariants: engine not quiescent after 60s ({getattr(self.ms, 'wait_reason', '')}); checking anyway")
        with self.ms.db() as c:
            return invariants.check(c, skip=arg.get("skip") or [])

    def expect_counts(self, arg: dict) -> list:
        with self.ms.db() as c:
            counts = {mb: len(uids) for mb, uids in self._named(dbmod.placements(c)).items()}
            total = dbmod.message_count(c)
        out = []
        for mb, n in arg.items():
            if mb == "total":
                if total != n:
                    out.append(f"expected {n} Message rows, found {total}")
            elif counts.get(mb, 0) != n:
                out.append(f"expected {n} messages in {mb}, found {counts.get(mb, 0)}")
        return out

    def expect_shown(self, arg: dict) -> list:
        with self.ms.db() as c:
            shown = self._named(dbmod.shown_counts_by_folder(c))
        return [f"expected {n} messages shown in {mb}, found {shown.get(mb, 0)}"
                for mb, n in arg.items() if shown.get(mb, 0) != n]

    def expect_stable(self, arg: dict) -> list:
        """The flapping detector: N further passes must not change any placement."""
        passes = int(arg.get("passes", 2))
        out = []
        with self.ms.db() as c:
            before = self._named(dbmod.placements(c))
        for i in range(passes):
            t = self.ms.sync_pass(timeout=float(arg.get("timeout", 180)), ignore_busy=self.ignore_busy)
            with self.ms.db() as c:
                after = self._named(dbmod.placements(c))
            changes = placement_changes(before, after)
            if changes:
                out.append(f"pass {i + 1}: {len(changes)} placement changes on an idle mailbox: "
                           + "; ".join(changes[:6]))
            # Deleting a message that was still placed at the start of the pass is churn; the
            # engine's deferred deletion of already-unlinked rows is not.
            placed = {pl.message_id for uids in before.values() for pl in uids.values()}
            gone = [mid for d in self.ms.deltas("Message", since=t) if d.type == "unpersist" for mid in d.ids() if mid in placed]
            if gone:
                out.append(f"pass {i + 1}: {len(gone)} placed messages were deleted on an idle mailbox")
            before = after
        return out

    def expect_log_absent(self, patterns) -> list:
        out = []
        for p in patterns:
            hits = self.ms.grep(p)
            if hits:
                out.append(f"log contains /{p}/ ({len(hits)}x): {hits[0].msg[:160]}")
        return out

    def expect_log_present(self, patterns) -> list:
        return [f"log does not contain /{p}/" for p in patterns if not self.ms.grep(p)]

    def expect_log_count(self, arg: dict) -> list:
        """{regex: n} or {regex: {min: a, max: b}}: how many log lines match, which bounds a
        loop the engine is meant to take a known number of times (e.g. a draining backlog)."""
        out = []
        for pattern, want in arg.items():
            n = len(self.ms.grep(pattern))
            lo, hi = (want, want) if isinstance(want, int) else (want.get("min", 0), want.get("max"))
            if n < lo or (hi is not None and n > hi):
                out.append(f"log matches /{pattern}/ {n}x, expected {want}")
        return out

    def expect_running(self, arg) -> list:
        return [] if self.ms.running else [f"mailsync is not running ({describe_exit(self.ms.exit_code)})"]

    def expect_exit(self, arg) -> list:
        code = self.ms.exit_code
        want = arg.get("code") if isinstance(arg, dict) else arg
        if code is None:
            return [f"expected mailsync to exit with {want}, but it is still running"]
        if want is not None and code != want:
            return [f"expected exit code {want}, got {describe_exit(code)}"]
        return []

    def expect_folder_status(self, arg: dict) -> list:
        status = self._named(self.ms.db_folders())
        out = []
        for path, wanted in arg.items():
            ls = status.get(path, {}).get("localStatus")
            if ls is None:
                out.append(f"folder {path} missing")
                continue
            for k, v in wanted.items():
                if ls.get(k) != v:
                    out.append(f"{path}.localStatus.{k} = {ls.get(k)!r}, expected {v!r}")
        return out

    def expect_server_counts(self, arg: dict) -> list:
        truth = self.server.truth()
        return [f"server has {len(truth.get(mb, {}))} in {mb}, expected {n}" for mb, n in arg.items()
                if len(truth.get(mb, {})) != n]

    def expect_server_uids(self, arg: dict) -> list:
        truth = self.server.truth()
        return [f"server has UIDs {sorted(truth.get(mb, {}))} in {mb}, expected {sorted(uids)}" for mb, uids in arg.items()
                if sorted(truth.get(mb, {})) != sorted(uids)]

    def expect_server_has(self, arg: dict) -> list:
        """{mailbox: [Message-ID | {message_id, flags}]}: each message is in that mailbox on
        the server, whatever the engine believes - what proves a task moved the message it was
        asked to. With `flags`, some copy there carries exactly those tracked flags, which is
        what tells a message's copies apart when they differ only in unread or starred."""
        truth = self.server.truth()
        problems = []
        for mb, wanted in arg.items():
            held = list(truth.get(mb, {}).values())
            for want in wanted:
                mid = normalize_message_id(want if isinstance(want, str) else want["message_id"])
                copies = [set(v["flags"]) & TRACKED_FLAGS for v in held if v["message_id"] == mid]
                if not copies:
                    problems.append(f"server has no {mid} in {mb}")
                elif isinstance(want, dict) and "flags" in want and set(want["flags"]) not in copies:
                    problems.append(f"server has {mid} in {mb} with flags {[sorted(c) for c in copies]}, "
                                    f"expected {sorted(want['flags'])}")
        return problems

    def expect_unchanged_since(self, arg) -> list:
        name = arg if isinstance(arg, str) else arg["snapshot"]
        with self.ms.db() as c:
            now = self._named(dbmod.placements(c))
        changes = placement_changes(self.snapshots[name], now)
        return [f"placements changed since snapshot {name}: " + "; ".join(changes[:6])] if changes else []

    def expect_smtp(self, arg: dict) -> list:
        """What the fake SMTP server received: count, recipients, headers."""
        smtp = getattr(self.server, "smtp", None)
        if smtp is None:
            return ["scenario expects SMTP but the server has no SMTP endpoint"]
        out = []
        msgs = list(smtp.messages)
        if "count" in arg and len(msgs) != arg["count"]:
            out.append(f"expected {arg['count']} messages over SMTP, got {len(msgs)}")
        for i, want in enumerate(arg.get("messages", [])):
            if i >= len(msgs):
                out.append(f"SMTP message {i} missing")
                continue
            m = msgs[i]
            if "recipients" in want and sorted(m.recipients) != sorted(want["recipients"]):
                out.append(f"SMTP message {i} recipients {m.recipients} != {want['recipients']}")
            for h, v in (want.get("headers") or {}).items():
                if m.header(h) != v:
                    out.append(f"SMTP message {i} header {h} = {m.header(h)!r}, expected {v!r}")
        return out

    def expect_rules_ready(self, arg: dict) -> list:
        """The one-shot `rulesReady` flag the client's mail rules run on. No message may carry
        it on more than one delta, counting every engine process the scenario ran, and every
        such delta must carry the body. `total: n` is how many messages carried it.
        `messages: {Message-ID or subject: n | {count, folders, fetched, metadata}}` pins one
        message: how many deltas carried it (0 or 1); mailboxes its `folders` snapshot must
        list on that delta; whether that delta is the one that fetched the body
        (fullSyncComplete) rather than one that read it back; and plugin ids whose metadata
        rode on it, which a later save of the same message can only have put there by being
        coalesced into the same delta."""
        with self.ms.db() as c:
            paths = {r[0]: r[1] for r in c.execute("SELECT id, path FROM Folder")}
        flagged, subjects = {}, {}
        for d in self.earlier_deltas + self.ms.deltas("Message"):
            if d.model_class != "Message" or d.type != "persist":
                continue
            for m in d.models:
                mid = normalize_message_id(m.get("hMsgId"))
                subjects[m.get("subject")] = mid
                if m.get("rulesReady"):
                    flagged.setdefault(mid, []).append(m)
        out = [f"{mid} carried rulesReady on {len(ms)} deltas" for mid, ms in flagged.items() if len(ms) > 1]
        out += [f"{mid}: rulesReady delta has no body" for mid, ms in flagged.items() if not ms[0].get("body")]
        if "total" in arg and len(flagged) != arg["total"]:
            out.append(f"expected {arg['total']} messages to carry rulesReady, saw {len(flagged)}: "
                       + ", ".join(f"{mid} ({ms[0].get('subject')})" for mid, ms in flagged.items()))
        for mid, want in (arg.get("messages") or {}).items():
            want = want if isinstance(want, dict) else {"count": want}
            key = normalize_message_id(mid) if "@" in mid else subjects.get(mid)
            if key is None:
                out.append(f"no message with subject {mid!r} was ever streamed")
                continue
            got = flagged.get(key, [])
            if "count" in want and len(got) != want["count"]:
                out.append(f"{mid}: expected rulesReady on {want['count']} deltas, saw {len(got)}")
            if not got:
                continue
            m = got[0]
            folders = sorted(self.server.scenario_name(paths.get(fid, fid)) for fid in (m.get("folders") or {}))
            missing = sorted(set(want.get("folders") or []) - set(folders))
            if missing:
                out.append(f"{mid}: rulesReady delta listed folders {folders}, missing {missing}")
            if "fetched" in want and bool(m.get("fullSyncComplete")) != bool(want["fetched"]):
                out.append(f"{mid}: rulesReady delta {'lacked' if want['fetched'] else 'carried'} fullSyncComplete")
            plugins = {e.get("pluginId") for e in m.get("metadata") or []}
            for plugin in want.get("metadata") or []:
                if plugin not in plugins:
                    out.append(f"{mid}: rulesReady delta has no {plugin} metadata (plugins: {sorted(plugins)})")
        if out:
            for d in self.earlier_deltas + self.ms.deltas("Message"):
                for m in d.models if d.model_class == "Message" else []:
                    if m.get("rulesReady") or "body" in m or m.get("metadata"):
                        self._note(f"Message delta at {d.t:.2f}s: {m.get('subject')!r} folders={sorted(m.get('folders') or {})} "
                                   f"rulesReady={bool(m.get('rulesReady'))} body={'body' in m} "
                                   f"fullSyncComplete={bool(m.get('fullSyncComplete'))} "
                                   f"metadata={[e.get('pluginId') for e in m.get('metadata') or []]} v={m.get('v')}")
        return out

    def expect_connection_error(self, arg: dict) -> list:
        """The ProcessState deltas from beginConnectionError / endConnectionError, which drive
        the client's offline banner. `reported: true` needs a connectionError: true among
        them, `cleared: true` needs the last one to be false."""
        states = [m.get("connectionError") for d in self.ms.deltas("ProcessState") for m in d.models]
        out = []
        if "reported" in arg and (True in states) != bool(arg["reported"]):
            out.append(f"expected connectionError {'to be' if arg['reported'] else 'never to be'} reported, "
                       f"ProcessState deltas were {states}")
        if arg.get("cleared") and (not states or states[-1] is not False):
            out.append(f"expected the connection error to be cleared, ProcessState deltas were {states}")
        return out

    def expect_deltas(self, arg: dict) -> list:
        out = []
        for cls, wants in arg.items():
            for typ, n in wants.items():
                got = sum(len(d.models) for d in self.ms.deltas(cls) if d.type == typ)
                if got != n:
                    out.append(f"expected {n} {cls} {typ} deltas, saw {got}")
        return out


def discover(scenarios_dir: Path) -> list:
    return sorted(scenarios_dir.glob("*.yaml"))


def available_server_kinds() -> set:
    kinds = {"fake"}
    env = os.environ.get("HARNESS_SERVERS")
    if env:
        return set(env.split(","))
    try:
        from .servers.dovecot import docker_available
        if docker_available():
            kinds.update({"dovecot", "cyrus"})
    except Exception:
        pass
    return kinds
