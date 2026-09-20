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
from . import mailgen
from .assertions import compare_placements, placement_changes
from .mailsync import MailsyncError, MailsyncProcess, account_json
from .servers.base import Server

RUNS_DIR = Path(__file__).resolve().parents[1] / "runs"


class ScenarioFailure(AssertionError):
    pass


class ScenarioSkipped(Exception):
    pass


@dataclass
class ServerSpec:
    kind: str          # fake | dovecot
    profile: str       # personality name or dovecot profile name
    options: dict = field(default_factory=dict)

    @classmethod
    def parse(cls, entry) -> "ServerSpec":
        if isinstance(entry, str):
            kind, _, profile = entry.partition(":")
            return cls(kind, profile or "dovecot")
        (kind, profile), = [(k, v) for k, v in entry.items() if k in ("fake", "dovecot")]
        options = {k: v for k, v in entry.items() if k not in ("fake", "dovecot")}
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
    if spec.kind == "dovecot":
        from .servers.dovecot import DovecotServer
        return DovecotServer(spec.profile, **spec.options)
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
        self.binary = binary
        self.keep = keep or bool(os.environ.get("HARNESS_KEEP"))
        self.name = f"{scenario['name']}-{spec.kind}-{spec.profile}".replace("/", "_")
        self.work = RUNS_DIR / self.name
        self.server: Optional[Server] = None
        self.ms: Optional[MailsyncProcess] = None
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

    def setup(self):
        self._check_host_alias()
        if self.work.exists():
            shutil.rmtree(self.work)
        self.work.mkdir(parents=True)
        self.server = make_server(self.spec, str(self.work / "server.log"))
        self.server.start()
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
        self.ms = MailsyncProcess(account, self.work, binary=self.binary, verbose=True)

    def teardown(self):
        if self.ms:
            code = self.ms.stop()
            self._note(f"mailsync stopped (exit {code})")
        if self.server:
            self.server.stop()
        if self.work.exists():
            (self.work / "report.txt").write_text("\n".join(self.report) + "\n")
        if not self.keep and not self.failures and self.work.exists():
            shutil.rmtree(self.work, ignore_errors=True)

    # -- steps --------------------------------------------------------------------------------

    def run(self) -> list:
        """Runs the whole scenario; returns the list of failures (empty = pass)."""
        started = False
        try:
            self.setup()
            self.ms.start()
            started = True
            self._note(f"mailsync started against {self.spec.id} on port {self.server.port}")
            for step in self.sc.get("steps") or []:
                self.run_step(step)
            self.check_expectations(self.sc.get("expect") or {})
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
                self.check_expectations(self.sc.get("expect") or {})
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

    def _snapshot(self, name: str):
        with self.ms.db() as c:
            snap = dbmod.placements(c)
        self.snapshots[name] = snap
        return snap

    def _restart(self, arg: dict):
        code = self.ms.stop()
        self._note(f"mailsync stopped for restart (exit {code})")
        account = self.ms.account
        binary = self.binary
        if arg.get("binary"):
            binary = Path(arg["binary"])
        self.ms = MailsyncProcess(account, self.work, binary=binary, verbose=True)
        self.ms.start()
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
        """{mailbox, uids} -> engine message ids, via the placements view."""
        with self.ms.db() as c:
            pl = dbmod.placements(c)
        folder = pl.get(sel["mailbox"], {})
        uids = parse_uids(sel["uids"]) if "uids" in sel else sorted(folder)
        missing = [u for u in uids if u not in folder]
        if missing:
            raise ScenarioFailure(f"messages {sel['mailbox']} UIDs {missing} are not in the engine's database")
        return [folder[u].message_id for u in uids]

    def _folder_json(self, path: str) -> dict:
        """Folder (or, on Gmail, Label) JSON as the client would pass it in a task."""
        with self.ms.db() as c:
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
            def fire(session):
                self._note(f"hook {at} fired on session {session.sid}: server.{op} {json.dumps(arg)}")
                self._server_op(op, arg)
            self.server.at(at, fire, once=True)
            return
        if at:
            self._note(f"(server {self.spec.kind} has no hook {at}; applying server.{op} immediately)")
        return self._server_op(op, arg)

    def _server_op(self, op: str, arg: dict):
        s = self.server
        if op == "expunge":
            s.expunge(arg["mailbox"], parse_uids(arg["uids"]))
        elif op == "flags":
            s.set_flags(arg["mailbox"], parse_uids(arg["uids"]), add=arg.get("add", []), remove=arg.get("remove", []))
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
        elif op == "set_uidvalidity":
            s.set_uidvalidity(arg["mailbox"], int(arg["value"]))
        elif op == "set_uidnext":
            s.set_uidnext(arg["mailbox"], int(arg["value"]))
        elif op == "drop_connections":
            s.drop_connections()
        else:
            raise ScenarioFailure(f"unknown server op {op!r}")

    def _client_step(self, op: str, arg: dict):
        if op == "wake":
            self.ms.wake()
            return
        if op == "task":
            task = dict(arg)
            if "messages" in task:
                task["messageIds"] = self._resolve_messages(task.pop("messages"))
            if isinstance(task.get("folder"), str):
                task["folder"] = self._folder_json(task["folder"])
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
        if op == "need_bodies":
            self.ms.need_bodies(self._resolve_messages(arg["messages"]))
            return
        raise ScenarioFailure(f"unknown client op {op!r}")

    # -- expectations ---------------------------------------------------------------------------

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
            problems.append(f"mailsync exited with {self.ms.exit_code}\n{self.ms.stderr_tail()}")
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
            local = dbmod.placements(c)
        truth = self.server.truth()
        return compare_placements(local, truth, mailboxes=arg.get("mailboxes"),
                                  check_flags=arg.get("flags", True), check_labels=arg.get("labels", False))

    def expect_counts(self, arg: dict) -> list:
        with self.ms.db() as c:
            counts = dbmod.counts_by_folder(c)
            total = dbmod.message_count(c)
        out = []
        for mb, n in arg.items():
            if mb == "total":
                if total != n:
                    out.append(f"expected {n} Message rows, found {total}")
            elif counts.get(mb, 0) != n:
                out.append(f"expected {n} messages in {mb}, found {counts.get(mb, 0)}")
        return out

    def expect_stable(self, arg: dict) -> list:
        """The flapping detector: N further passes must not change any placement."""
        passes = int(arg.get("passes", 2))
        out = []
        with self.ms.db() as c:
            before = dbmod.placements(c)
        for i in range(passes):
            t = self.ms.sync_pass(timeout=float(arg.get("timeout", 180)), ignore_busy=self.ignore_busy)
            with self.ms.db() as c:
                after = dbmod.placements(c)
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

    def expect_running(self, arg) -> list:
        return [] if self.ms.running else [f"mailsync is not running (exit {self.ms.exit_code})"]

    def expect_exit(self, arg) -> list:
        code = self.ms.exit_code
        want = arg.get("code") if isinstance(arg, dict) else arg
        if code is None:
            return [f"expected mailsync to exit with {want}, but it is still running"]
        if want is not None and code != want:
            return [f"expected exit code {want}, got {code}"]
        return []

    def expect_folder_status(self, arg: dict) -> list:
        status = self.ms.db_folders()
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

    def expect_unchanged_since(self, arg) -> list:
        name = arg if isinstance(arg, str) else arg["snapshot"]
        with self.ms.db() as c:
            now = dbmod.placements(c)
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
            kinds.add("dovecot")
    except Exception:
        pass
    return kinds
