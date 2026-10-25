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
import hashlib
import json
import os
import random
import re
import shutil
import socket
import sqlite3
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import yaml
from datetime import datetime, timedelta

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

    def _check_sasl_plugins(self):
        """libetpan authenticates SMTP through Cyrus SASL, which loads PLAIN/LOGIN from plugin
        files (libsasl2-modules on Debian/Ubuntu). Without them the engine never sends AUTH and
        every send fails with "ErrorAuthentication LibEtPan code: 296", which reads like a
        credentials or TLS problem rather than a missing package."""
        if not sys.platform.startswith("linux") or "SendDraftTask" not in json.dumps(self.sc.get("steps") or []):
            return
        dirs = [Path(d) for d in os.environ.get("SASL_PATH", "").split(":") if d]
        dirs += [Path("/usr/lib/sasl2"), Path("/usr/lib64/sasl2"), Path("/usr/local/lib/sasl2")]
        dirs += list(Path("/usr/lib").glob("*/sasl2"))
        if not any(next(d.glob(f"lib{mech}.so*"), None) for d in dirs for mech in ("plain", "login")):
            raise ScenarioFailure(
                "no Cyrus SASL PLAIN/LOGIN plugin found (searched SASL_PATH and /usr/lib*/sasl2), so "
                "the engine cannot AUTH to SMTP and every send fails with ErrorAuthentication. "
                "Install libsasl2-modules (apt) or cyrus-sasl-plain (dnf)."
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
        self._check_sasl_plugins()
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
        env = self.server.engine_env() if self.server else {}
        env.update({k: str(v) for k, v in (self.sc.get("env") or {}).items()})
        return env

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
        elif verb == "db.sql":
            result = self._db_sql(arg)
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

    def _db_sql(self, statements) -> int:
        """Writes the engine's database while it is stopped (`restart: {before: [...]}`): how a
        scenario reproduces a stored state that users' databases carry when the engine path
        that produced it is unknown. Every derived layer must be written consistently, or the
        invariants report the scenario's own edit."""
        if self.ms.running:
            raise ScenarioFailure("db.sql writes a stopped engine's database; use it under restart: {before: [...]}")
        conn = sqlite3.connect(str(self.ms.db_path), timeout=30)
        try:
            changed = sum(conn.execute(sql).rowcount for sql in ([statements] if isinstance(statements, str) else statements))
            conn.commit()
        finally:
            conn.close()
        self._note(f"db.sql changed {changed} rows")
        return changed

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
                if op == "reject":   # `close: true` ends the session after the NO, as a server does after a rejected login
                    session.reject_next = (arg.get("code"), arg.get("text", "Command failed."), bool(arg.get("close")))
                elif op == "desync":
                    session.desynced = True
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
        elif op == "index":
            s.index()
        elif op == "pause":
            pass  # with `at` + `delay`: holds one reply without changing the server
        elif op == "reject":
            pass  # only meaningful with `at` on the fake, which answers the hooked command NO
        elif op == "desync":
            pass  # only meaningful with `at` on the fake: that session answers every further command BAD
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
                draft["files"] = [self._attachment_file(f) for f in draft.get("files") or []]
            task_id = self.ms.queue_task(task)
            self._note(f"queued {task['__cls']} as {task_id}")
            return task_id
        if op == "undo_task":
            return self._undo_task(arg)
        if op == "need_bodies":
            self.ms.need_bodies(self._resolve_messages(arg["messages"]))
            return
        if op == "search":
            return self._search(arg)
        if op == "search_cancel":   # {of: label} or {searchId: id}
            search_id = self.labels[arg["of"]] if "of" in arg else arg["searchId"]
            self.ms.send({"type": "search-remote-cancel", "searchId": search_id})
            return None
        if op == "search_without_id":
            self.ms.send({"type": "search-remote", "criteria": ["all"]})
            return None
        if op == "send":   # a packet exactly as given, for requests the client would never form
            self.ms.send(dict(arg))
            return None
        raise ScenarioFailure(f"unknown client op {op!r}")

    def _search(self, arg: dict) -> str:
        """`search-remote` on stdin, as the client sends it for a submitted query: `criteria`
        and `gmailRaw` pass through, `folders` (scenario paths) become `folderIds`. Waits for
        the final ProcessSearchResult unless `wait: false`; returns the search id for `expect: search`."""
        search_id = f"search-{int(time.time() * 1000)}-{len(self.labels)}"
        packet = {"type": "search-remote", "searchId": search_id}
        for key in ("criteria", "gmailRaw", "limit"):
            if key in arg:
                packet[key] = arg[key]
        for key in ("criteria", "gmailRaw"):
            if key in packet:
                packet[key] = self._resolve_search_dates(packet[key])
        if "folders" in arg:
            packet["folderIds"] = [self._folder_json(p)["id"] for p in arg["folders"]]
        self.ms.send(packet)
        if arg.get("wait", True):
            self.ms.wait_for(lambda: self._search_result(search_id) is not None,
                             float(arg.get("timeout", 60)), f"search {search_id} to finish")
        return search_id

    def _resolve_search_dates(self, criteria):
        """`@today`, `@today-3`, `@today+1` -> YYYY-MM-DD in the engine's local zone, because a
        server stamps APPENDed mail with the time it arrived; `@repeat:a:1025` -> "aaa..."."""
        if isinstance(criteria, list):
            return [self._resolve_search_dates(c) for c in criteria]
        if isinstance(criteria, str) and criteria.startswith("@repeat:"):   # @repeat:<text>:<count>
            _, text, count = criteria.split(":")
            return text * int(count)
        m = re.match(r"^@today([+-]\d+)?$", criteria) if isinstance(criteria, str) else None
        if not m:
            return criteria
        return (datetime.now() + timedelta(days=int(m.group(1) or 0))).strftime("%Y-%m-%d")

    def _search_result(self, search_id: str, done: bool = True):
        """The last ProcessSearchResult the engine emitted for this search (the final one when `done`)."""
        found = [m for d in self.ms.deltas("ProcessSearchResult") for m in d.models
                 if m.get("id") == search_id and (m.get("done") or not done)]
        return found[-1] if found else None

    def _attachment_file(self, spec: dict) -> dict:
        """A draft attachment given as {filename, bytes, contentType?, contentId?}: write that
        many pseudo-random bytes where the engine's MailUtils::pathForFile looks for the file
        and return the File JSON the client would send."""
        if "bytes" not in spec:
            return spec
        file_id = spec.get("id") or hashlib.sha1(spec["filename"].encode()).hexdigest()[:24]
        folder = self.ms.config_dir / "files" / file_id[0:2] / file_id[2:4] / file_id
        folder.mkdir(parents=True, exist_ok=True)
        data = random.Random(file_id).randbytes(int(spec["bytes"]))
        (folder / spec["filename"]).write_bytes(data)
        out = {"id": file_id, "__cls": "File", "aid": self.ms.account["id"], "v": 0,
               "filename": spec["filename"], "size": len(data),
               "contentType": spec.get("contentType", "application/octet-stream")}
        if spec.get("contentId"):
            out["contentId"] = spec["contentId"]
        return out

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

    def expect_migrate_output(self, pattern) -> list:
        """The last `--mode migrate` stdout, whitespace collapsed, matches `pattern`. The
        client opens its progress window on "Running Migration"."""
        out = self.ms.migrate_output or ""
        return [] if re.search(pattern, out) else [f"migrate said {out!r}, expected /{pattern}/"]

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

    def expect_folder_roles(self, arg: dict) -> list:
        folders = self._named(self.ms.db_folders())
        return [f"folder {path} has role {folders.get(path, {}).get('role')!r}, expected {role!r}"
                for path, role in arg.items() if folders.get(path, {}).get("role") != role]

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

    def expect_bodies(self, arg: dict) -> list:
        """{mailbox: {Message-ID: {contains: [...], lacks: [...]}}}: every server copy of that
        message in that mailbox, and the body the engine stored for it, contain and lack those
        strings - what tells the sender's copy of a tracked message from the recipients' (fake
        servers only)."""
        store = getattr(self.server, "store", None)
        if store is None:
            return ["bodies needs a fake server"]
        out = []
        for mb, messages in arg.items():
            box = store.get(mb)
            for mid, want in messages.items():
                mid = normalize_message_id(mid)
                copies = [m for m in (box.messages if box else []) if normalize_message_id(m.message_id) == mid]
                if not copies:
                    out.append(f"server has no {mid} in {mb}")
                with self.ms.db() as c:
                    row = c.execute("SELECT b.value FROM Message m JOIN MessageBody b ON b.id = m.id "
                                    "WHERE m.headerMessageId = ?", (mid,)).fetchone()
                if row is None:
                    out.append(f"no stored body for {mid}")
                texts = [(f"{mb} UID {m.uid}", m.raw.decode("utf-8", "replace")) for m in copies]
                texts += [("stored body", row[0] or "")] if row else []
                for where, text in texts:
                    out += [f"{mid} {where} lacks {s!r}" for s in want.get("contains", []) if s not in text]
                    out += [f"{mid} {where} contains {s!r}" for s in want.get("lacks", []) if s in text]
        return out

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

    def expect_graph(self, arg: dict) -> list:
        """What the fake Graph server sent: `count`; per message `recipients` (To, Cc and the
        draft's bccRecipients), `headers` (null for one that must be absent), `body_contains` /
        `body_lacks` strings and `attachments` (file names, wherever they ended up); `drafts` unsent drafts left on the
        server; `upload_sessions` attachments that went through an upload session;
        `graph_token_requests` refreshes that asked for Graph scopes; `distinct_tokens` two path
        regexes whose requests used different access tokens; `max_request_bytes` the
        largest request body the engine sent."""
        graph = getattr(self.server, "graph", None)
        if graph is None:
            return ["scenario expects Graph but the server has no `graph: true`"]
        out = []
        sent = list(graph.sent)
        if "count" in arg and len(sent) != arg["count"]:
            out.append(f"expected {arg['count']} messages sent through Graph, got {len(sent)}")
        for i, want in enumerate(arg.get("messages", [])):
            if i >= len(sent):
                out.append(f"Graph message {i} missing")
                continue
            m = sent[i]
            if "recipients" in want and sorted(m.recipients()) != sorted(want["recipients"]):
                out.append(f"Graph message {i} recipients {m.recipients()} != {want['recipients']}")
            for h, v in (want.get("headers") or {}).items():
                if m.header(h) != v:
                    out.append(f"Graph message {i} header {h} = {m.header(h)!r}, expected {v!r}")
            sent_mime = m.mime.decode("utf-8", "replace")
            out += [f"Graph message {i} lacks {s!r}" for s in want.get("body_contains", []) if s not in sent_mime]
            out += [f"Graph message {i} contains {s!r}" for s in want.get("body_lacks", []) if s in sent_mime]
            if "attachments" in want and m.attachment_names() != sorted(want["attachments"]):
                out.append(f"Graph message {i} attachments {m.attachment_names()} != {sorted(want['attachments'])}")
        if "drafts" in arg and len(graph.drafts()) != arg["drafts"]:
            out.append(f"expected {arg['drafts']} unsent Graph drafts, found {len(graph.drafts())}")
        if "upload_sessions" in arg and len(graph.uploads) != arg["upload_sessions"]:
            out.append(f"expected {arg['upload_sessions']} upload sessions, saw {len(graph.uploads)}")
        if "graph_token_requests" in arg:
            got = sum(1 for scope in graph.token_scopes if "graph.microsoft.com" in scope)
            if got != arg["graph_token_requests"]:
                out.append(f"expected {arg['graph_token_requests']} Graph token requests, saw {got}")
        if "distinct_tokens" in arg:
            # [regex, regex]: the requests matching each path regex were made with different tokens
            def tokens(rx):
                return {r["headers"].get("authorization") for r in graph.requests
                        if r["method"] != "GET" and re.search(rx, r["path"])}
            first, second = (tokens(rx) for rx in arg["distinct_tokens"])
            if not first or not second or first & second:
                out.append(f"expected different tokens for {arg['distinct_tokens']}, saw {first} and {second}")
        if "max_request_bytes" in arg:
            largest = max((r["bytes"] for r in graph.requests), default=0)
            if largest > arg["max_request_bytes"]:
                out.append(f"largest Graph request was {largest} bytes, over {arg['max_request_bytes']}")
        return out

    def expect_test_auth(self, arg: dict) -> list:
        """Run `--mode test` (the client's account-add check) and compare its JSON: `error` (the
        error code, or null), `error_service`, and `log` / `log_absent` regexes on its log."""
        r = self.ms.run_mode("test")
        try:
            resp = json.loads(r.stdout.strip().splitlines()[-1])
        except (IndexError, json.JSONDecodeError):
            return [f"--mode test printed no JSON (exit {r.returncode}): {r.stdout[-300:]} {r.stderr[-300:]}"]
        self._note(f"--mode test said: error={resp.get('error')!r} service={resp.get('error_service')!r}")
        out = []
        if "error" in arg and resp.get("error") != arg["error"]:
            out.append(f"--mode test error {resp.get('error')!r}, expected {arg['error']!r}: {resp.get('log', '')[-300:]}")
        if "error_service" in arg and resp.get("error_service") != arg["error_service"]:
            out.append(f"--mode test error_service {resp.get('error_service')!r}, expected {arg['error_service']!r}")
        log = resp.get("log") or ""
        for pattern in arg.get("log", []):
            if not re.search(pattern, log):
                out.append(f"--mode test log has no match for {pattern!r}")
        for pattern in arg.get("log_absent", []):
            if re.search(pattern, log):
                out.append(f"--mode test log matches {pattern!r}")
        return out

    def expect_task_error(self, arg: dict) -> list:
        """{label: error key, or null for success, or {key, debuginfo: regex}}: how each
        labelled task finished."""
        out = []
        for label, want in arg.items():
            with self.ms.db() as c:
                row = c.execute("SELECT data FROM Task WHERE id = ?", (self.labels[label],)).fetchone()
            error = (json.loads(row[0]).get("error") or {}) if row else {}
            got = error.get("key") if error else None
            want_key = want.get("key") if isinstance(want, dict) else want
            if got != want_key:
                out.append(f"task {label} finished with error {got!r} ({error.get('debuginfo', '')}), expected {want_key!r}")
            elif isinstance(want, dict) and "debuginfo" in want and not re.search(want["debuginfo"], error.get("debuginfo", "")):
                out.append(f"task {label} error debuginfo {error.get('debuginfo', '')!r} does not match {want['debuginfo']!r}")
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

    def expect_contacts(self, arg: dict) -> list:
        """Contact rows. `mail: {email: name}` checks found-in-mail contacts by email (`null`:
        there must be none); `carddav: {id: {email, name}}` checks address-book contacts by id.
        Each contact found must have the ContactSearch row the engine indexes for it (email
        with `@` as a space, then name), which is what composer autocomplete matches."""
        out = []

        def check(label, row, want):
            if want is None:
                if row:
                    out.append(f"contact {label}: expected none, found one named {row['name']!r}")
                return
            if not row:
                out.append(f"contact {label}: missing")
                return
            for key, value in (want if isinstance(want, dict) else {"name": want}).items():
                if row[key] != value:
                    out.append(f"contact {label}: {key} {row[key]!r}, expected {value!r}")
            indexed = row["email"].replace("@", " ", 1) + " " + row["name"]
            if row["search"] != indexed:
                out.append(f"contact {label}: ContactSearch holds {row['search']!r}, expected {indexed!r}")

        with self.ms.db() as c:
            for email, want in (arg.get("mail") or {}).items():
                check(email, dbmod.contact(c, "mail", email=email), want)
            for cid, want in (arg.get("carddav") or {}).items():
                check(cid, dbmod.contact(c, "carddav", id=cid), want)
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

    def expect_search(self, arg: dict) -> list:
        """{label: {message_ids: [Message-ID headers], error, folder_errors}}: the final
        ProcessSearchResult for the search labelled `label` holds exactly those messages' threads."""
        out = []
        for label, want in arg.items():
            got = self._search_result(self.labels[label])
            if got is None:
                out.append(f"search {label}: no final ProcessSearchResult")
                continue
            if got.get("error") != want.get("error"):
                out.append(f"search {label}: error {got.get('error')!r}, expected {want.get('error')!r}")
            if "message_ids" in want:
                # The engine answers with threads; every scenario message is its own thread,
                # so the expected threads are those of the named messages.
                wanted = [h.strip("<>") for h in want["message_ids"]]
                with self.ms.db() as c:
                    rows = c.execute("SELECT headerMessageId, threadId FROM Message").fetchall()
                thread_of = {(r["headerMessageId"] or "").strip("<>"): r["threadId"] for r in rows}
                missing = [h for h in wanted if h not in thread_of]
                if missing:
                    out.append(f"search {label}: messages {missing} are not in the engine's database")
                expected = sorted({thread_of[h] for h in wanted if h in thread_of})
                got_threads = got.get("threadIds", [])
                if len(set(got_threads)) != len(got_threads):
                    out.append(f"search {label}: threadIds repeat: {got_threads}")
                if sorted(set(got_threads)) != expected:
                    names = {v: k for k, v in thread_of.items()}
                    out.append(f"search {label}: matched {sorted(names.get(t, t) for t in got_threads)}, expected {sorted(wanted)}")
            if "folder_errors" in want and len(got.get("errors", [])) != want["folder_errors"]:
                out.append(f"search {label}: folder errors {got.get('errors')}, expected {want['folder_errors']}")
        return out

    def expect_no_search_without_id(self, arg) -> list:
        """A request without a searchId is never answered: no ProcessSearchResult has an empty id."""
        stray = [m for d in self.ms.deltas("ProcessSearchResult") for m in d.models if not m.get("id")]
        return [f"cancel produced results {stray}"] if stray else []

    def expect_search_not_done(self, labels) -> list:
        """[label, ...]: those searches were superseded, so the engine never finished them."""
        return [f"search {label} reported done: {self._search_result(self.labels[label])}"
                for label in labels if self._search_result(self.labels[label]) is not None]

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
