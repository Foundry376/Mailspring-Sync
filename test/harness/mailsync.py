"""
Drives a real mailsync process the way the Mailspring client does: account JSON on the
command line, newline-delimited JSON commands on stdin, the delta stream on stdout, and
the engine log in CONFIG_DIR_PATH. Nothing here touches the engine's source; every signal
the harness relies on is one the client already depends on or one that is logged today.

Quiescence is inferred, not signalled. The engine has no "done" event, so a process is
considered quiescent when all of these hold at once:

  - every Folder's localStatus has busy == false and syncedMinUID <= 1 (initial sync done)
  - the background worker's last log line is "Sync loop complete." and it is older than
    the settle window (syncNow() logs that line once per iteration and loops immediately
    while there is more to do, so a stale one means the worker went to sleep)
  - the foreground worker's last IMAP command is IDLE (verbose log) or its last engine
    line is "Idling on folder ..." (non-verbose)
  - no delta has arrived within the settle window
"""
import contextlib
import json
import os
import re
import signal
import sqlite3
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]


def describe_exit(code: Optional[int]) -> str:
    """A negative returncode is a signal, not an exit status: a migrate or sync the OS killed
    (e.g. SIGKILL under memory pressure) must not read like an engine error."""
    if code is None or code >= 0:
        return f"exit {code}"
    try:
        name = signal.Signals(-code).name
    except ValueError:
        name = "unknown signal"
    return f"killed by signal {-code} ({name})"

LOG_LINE = re.compile(
    r"^(?P<pid>\d+) \[(?P<ts>[^\]]+)\] \[(?P<thread>[^\]]+)\] \[(?P<level>[^\]]+)\] (?P<msg>.*)$"
)
IMAP_LINE = re.compile(r"^(?P<dir>sent|recv|sent-private|error-parse|error-received) (?P<line>.*)$")


class MailsyncError(RuntimeError):
    pass


def find_binary() -> Path:
    """MAILSYNC_BIN, else the client's app/ copy, else the Linux cmake output, else Xcode's."""
    candidates = []
    if os.environ.get("MAILSYNC_BIN"):
        candidates.append(Path(os.environ["MAILSYNC_BIN"]))
    candidates.append(REPO_ROOT.parent / "app" / "mailsync")
    candidates.append(REPO_ROOT / "mailsync")
    derived = Path.home() / "Library/Developer/Xcode/DerivedData"
    if derived.exists():
        candidates += sorted(derived.glob("MailSync-*/Build/Products/*/mailsync"), reverse=True)
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    raise MailsyncError(
        "No mailsync binary found. Build it (see BUILDING.md) or set MAILSYNC_BIN. Tried: "
        + ", ".join(str(c) for c in candidates)
    )


def account_json(
    imap_host: str,
    imap_port: int,
    *,
    account_id: str = "c0ffee-harness",
    email: str = "test@example.test",
    username: str = "test",
    password: str = "pass",
    provider: str = "imap",
    smtp_host: str = "127.0.0.1",
    smtp_port: int = 10025,
    imap_security: str = "none",
    smtp_security: str = "none",
    container_folder: str = "",
    extra_settings: Optional[dict] = None,
) -> dict:
    """The minimum Account the engine's Account::valid() accepts, matching what the client's
    onboarding produces for a generic IMAP account. Account ids starting with c-f get a
    startDelay() of 0 so the background worker starts immediately."""
    settings = {
        "imap_host": imap_host,
        "imap_port": imap_port,
        "imap_username": username,
        "imap_password": password,
        "imap_security": imap_security,
        "imap_allow_insecure_ssl": imap_security != "none",
        "smtp_host": smtp_host,
        "smtp_port": smtp_port,
        "smtp_username": username,
        "smtp_password": password,
        "smtp_security": smtp_security,
        "smtp_allow_insecure_ssl": smtp_security != "none",
        "container_folder": container_folder,
        "create_helper_folders": False,
    }
    settings.update(extra_settings or {})
    return {
        "id": account_id,
        "__cls": "Account",
        "provider": provider,
        "emailAddress": email,
        "name": "Harness Test",
        "settings": settings,
        "aliases": [],
        "label": email,
    }


@dataclass
class Delta:
    t: float
    type: str          # persist | unpersist
    model_class: str   # Message, Thread, Folder, Label, Task, ...
    models: list
    raw: dict

    def ids(self) -> list:
        return [m.get("id") for m in self.models]


@dataclass
class LogLine:
    t: float           # seconds since process start (from our clock, not the engine's)
    thread: str
    level: str
    msg: str
    imap_dir: Optional[str] = None   # sent / recv / sent-private when the line is IMAP traffic
    imap_line: Optional[str] = None


@dataclass
class ProcessState:
    deltas: list = field(default_factory=list)
    log: list = field(default_factory=list)
    stdout_noise: list = field(default_factory=list)   # non-JSON stdout, e.g. {"error": ...}
    folder_status: dict = field(default_factory=dict)  # folder id -> latest localStatus
    folder_paths: dict = field(default_factory=dict)   # folder id -> path
    last_delta_t: float = 0.0


class MailsyncProcess:
    def __init__(
        self,
        account: dict,
        work_dir: Path,
        binary: Optional[Path] = None,
        verbose: bool = True,
        identity: Optional[dict] = None,
        env: Optional[dict] = None,
    ):
        self.account = account
        self.work_dir = Path(work_dir)
        self.config_dir = self.work_dir / "config"
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self.verbose = verbose
        self.identity = identity
        self.extra_env = env or {}
        self.binary = self._launchable_binary(Path(binary).resolve() if binary else find_binary())
        self.proc: Optional[subprocess.Popen] = None
        self.state = ProcessState()
        self._lock = threading.Lock()
        self._t0 = 0.0
        self._threads: list = []
        self._log_pos = 0
        self._log_lock = threading.Lock()
        self._stopped = threading.Event()
        self.wait_reason = ""
        self.migrate_output = ""
        self.stderr_path = self.work_dir / "stderr.log"

    # -- lifecycle ----------------------------------------------------------------------

    def _launchable_binary(self, binary: Path) -> Path:
        # Release builds refuse to start unless argv[0] contains "mailspring" (main.cpp,
        # the executable-path check). A symlink inside the work dir satisfies it regardless
        # of where the real binary lives.
        link_dir = self.work_dir / "mailspring-bin"
        link_dir.mkdir(parents=True, exist_ok=True)
        link = link_dir / "mailsync"
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(binary)
        return link

    def _env(self) -> dict:
        env = dict(os.environ)
        env.update(
            CONFIG_DIR_PATH=str(self.config_dir),
            # Only the metadata worker talks to the identity server, and with --identity
            # null it exits immediately. The variable must still be set or main() bails.
            IDENTITY_SERVER="http://127.0.0.1:9/unreachable",
        )
        env.update(self.extra_env)
        return env

    def _identity_arg(self) -> str:
        return json.dumps(self.identity) if self.identity else "null"

    def run_mode(self, mode: str, timeout: float = 120) -> subprocess.CompletedProcess:
        """Run a one-shot mode (migrate, test, reset) and return its result."""
        return subprocess.run(
            [str(self.binary), "--mode", mode, "--identity", self._identity_arg(),
             "--account", json.dumps(self.account)],
            env=self._env(), capture_output=True, text=True, timeout=timeout,
        )

    def migrate(self):
        r = self.run_mode("migrate")
        # e.g. "Migration V10: 376 placements created, 10 messages without a copy"
        self.migrate_output = " ".join(r.stdout.split())
        if r.returncode < 0:
            raise MailsyncError(f"migrate {describe_exit(r.returncode)}: {r.stdout[-500:]} {r.stderr[-500:]}")
        if r.returncode != 0 or '"error":null' not in r.stdout.replace(" ", ""):
            hint = " (exit 2 with no output is the executable-path check: argv[0] must contain 'mailspring')" if r.returncode == 2 else ""
            raise MailsyncError(f"migrate failed ({r.returncode}){hint}: {r.stdout[-500:]} {r.stderr[-500:]}")

    def start(self):
        # The client runs `--mode migrate` before every launch (application.ts), and it is
        # the only mode that upgrades the schema: a build started on an older database
        # without it would run against tables it does not have.
        self.migrate()
        args = [str(self.binary), "--mode", "sync"]
        if self.verbose:
            args.append("--verbose")
        args += ["--identity", self._identity_arg(), "--account", json.dumps(self.account)]
        self._t0 = time.monotonic()
        self.proc = subprocess.Popen(
            args, env=self._env(), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=open(self.stderr_path, "wb"), bufsize=1, text=True,
        )
        for target in (self._read_stdout, self._tail_log):
            t = threading.Thread(target=target, daemon=True)
            t.start()
            self._threads.append(t)

    def stop(self, timeout: float = 5.0) -> Optional[int]:
        if not self.proc:
            return None
        if self.proc.poll() is None:
            try:
                self.proc.stdin.close()
            except Exception:
                pass
            self.proc.terminate()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self._stopped.set()
        for t in self._threads:
            t.join(timeout=2)
        self._drain_log()
        return self.proc.returncode

    @property
    def exit_code(self) -> Optional[int]:
        return self.proc.poll() if self.proc else None

    @property
    def running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def elapsed(self) -> float:
        return time.monotonic() - self._t0

    # -- stdin commands -------------------------------------------------------------------

    def send(self, packet: dict):
        if not self.running:
            raise MailsyncError(f"mailsync is not running ({describe_exit(self.exit_code)})")
        self.proc.stdin.write(json.dumps(packet) + "\n")
        self.proc.stdin.flush()

    def wake(self):
        """Ends the background worker's 120s sleep and re-marks folders busy, exactly as the
        client does when the user clicks Sync Mail."""
        self.send({"type": "wake-workers"})

    def queue_task(self, task: dict) -> str:
        """Queue a task the way Actions.queueTask does. `task` needs __cls and the fields
        that class's performLocal/performRemote read; id/accountId/status are filled in."""
        task = dict(task)
        task.setdefault("id", f"task-{int(time.time() * 1000)}-{len(self.state.deltas)}")
        task.setdefault("aid", self.account["id"])   # MailModel's JSON key for accountId
        task.setdefault("status", "local")
        task.setdefault("v", 0)
        task.setdefault("metadata", [])
        self.send({"type": "queue-task", "task": task})
        return task["id"]

    def need_bodies(self, message_ids: Iterable[str]):
        self.send({"type": "need-bodies", "ids": list(message_ids)})

    # -- observation ----------------------------------------------------------------------

    def _read_stdout(self):
        for line in self.proc.stdout:
            line = line.strip()
            if not line:
                continue
            t = self.elapsed()
            try:
                d = json.loads(line)
            except json.JSONDecodeError:
                with self._lock:
                    self.state.stdout_noise.append((t, line))
                continue
            if "modelClass" not in d:
                with self._lock:
                    self.state.stdout_noise.append((t, line))
                continue
            delta = Delta(t, d.get("type", ""), d["modelClass"], d.get("modelJSONs", []), d)
            with self._lock:
                self.state.deltas.append(delta)
                self.state.last_delta_t = t
                if delta.model_class == "Folder":
                    for m in delta.models:
                        if delta.type == "persist":
                            self.state.folder_status[m["id"]] = m.get("localStatus", {})
                            self.state.folder_paths[m["id"]] = m.get("path")
                        else:
                            self.state.folder_status.pop(m["id"], None)

    @property
    def log_path(self) -> Path:
        return self.config_dir / f"mailsync-{self.account['id']}.log"

    def _tail_log(self):
        while not self._stopped.is_set():
            self._drain_log()
            time.sleep(0.1)

    def _drain_log(self):
        if not self.log_path.exists():
            return
        with self._log_lock:   # the tailer thread and waiters both drain
            # spdlog rotates at 5 MB (mailsync-<id>.log -> mailsync-<id>.1.log). --verbose
            # logs every IMAP line, so a large mailbox rotates several times per run.
            if self.log_path.stat().st_size < self._log_pos:
                rotated = self.log_path.with_name(self.log_path.name + ".1")
                if rotated.exists():
                    with open(rotated, "rb") as f:
                        f.seek(self._log_pos)
                        tail = f.read()
                    cut = tail.rfind(b"\n")
                    if cut >= 0:
                        self._ingest_log(tail[:cut])
                self._log_pos = 0
            with open(self.log_path, "rb") as f:
                f.seek(self._log_pos)
                chunk = f.read()
            # only consume complete lines; a line the engine is still writing waits for next time
            cut = chunk.rfind(b"\n")
            if cut < 0:
                return
            self._log_pos += cut + 1
            self._ingest_log(chunk[:cut])

    def _ingest_log(self, chunk: bytes):
        t = self.elapsed()
        new = []
        for raw in chunk.decode("utf-8", "replace").split("\n"):
            m = LOG_LINE.match(raw.rstrip("\r"))
            if not m:
                continue  # continuation lines of multi-line IMAP literals
            ll = LogLine(t, m["thread"], m["level"], m["msg"])
            im = IMAP_LINE.match(ll.msg)
            if im:
                ll.imap_dir, ll.imap_line = im["dir"], im["line"]
            new.append(ll)
        with self._lock:
            self.state.log.extend(new)

    def deltas(self, model_class: Optional[str] = None, since: float = 0.0) -> list:
        with self._lock:
            return [d for d in self.state.deltas
                    if d.t >= since and (model_class is None or d.model_class == model_class)]

    def log(self, thread: Optional[str] = None, engine_only: bool = False, since: float = 0.0) -> list:
        with self._lock:
            return [l for l in self.state.log
                    if l.t >= since and (thread is None or l.thread == thread)
                    and (not engine_only or l.imap_dir is None)]

    def imap_transcript(self, thread: Optional[str] = None) -> list:
        """(thread, sent|recv, line) for every IMAP line the engine logged (needs --verbose)."""
        return [(l.thread, l.imap_dir, l.imap_line) for l in self.log(thread) if l.imap_dir]

    def log_contains(self, pattern: str, since: float = 0.0) -> bool:
        rx = re.compile(pattern)
        return any(rx.search(l.msg) for l in self.log(since=since))

    def grep(self, pattern: str, since: float = 0.0) -> list:
        rx = re.compile(pattern)
        return [l for l in self.log(since=since) if rx.search(l.msg)]

    # -- database -------------------------------------------------------------------------

    @property
    def db_path(self) -> Path:
        return self.config_dir / "edgehill.db"

    def db(self):
        """`with proc.db() as conn:` - closed on exit (sqlite3's own context manager only
        commits, and a lingering connection re-creates the WAL files after teardown).
        The engine owns writes; the harness only reads. query_only enforces that at the
        connection level (mode=ro cannot be used: a WAL database needs the -shm file, which a
        read-only opener may not create before the engine has opened it)."""
        conn = sqlite3.connect(str(self.db_path), timeout=10)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA query_only = 1")
        return contextlib.closing(conn)

    def db_folders(self) -> dict:
        """path -> {id, role, localStatus} straight from the database."""
        with self.db() as c:
            rows = c.execute("SELECT id, path, role, data FROM Folder").fetchall()
        out = {}
        for r in rows:
            data = json.loads(r["data"])
            out[r["path"]] = {"id": r["id"], "role": r["role"], "localStatus": data.get("localStatus", {})}
        return out

    # -- waiting --------------------------------------------------------------------------

    def wait_for(self, predicate: Callable[[], bool], timeout: float, what: str = "condition",
                 poll: float = 0.25) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.running:
                raise MailsyncError(f"mailsync stopped ({describe_exit(self.exit_code)}) while waiting for {what}"
                                    f"\n--- stderr ---\n{self.stderr_tail()}")
            if predicate():
                return
            time.sleep(poll)
        raise TimeoutError(f"timed out after {timeout}s waiting for {what}\n{self.describe()}")

    def wait_for_log(self, pattern: str, timeout: float = 60, since: float = 0.0) -> LogLine:
        rx = re.compile(pattern)
        found = []

        def check():
            for l in self.log(since=since):
                if rx.search(l.msg):
                    found.append(l)
                    return True
            return False

        self.wait_for(check, timeout, f"log line /{pattern}/")
        return found[0]

    def _folders_idle(self, ignore_busy: Iterable[str] = ()) -> bool:
        folders = self.db_folders()
        if not folders:
            return False
        for path, f in folders.items():
            if path in ignore_busy:
                continue
            ls = f["localStatus"]
            if ls.get("busy") is True:   # folders the engine never syncs (\All) have no busy key
                return False
            if ls.get("syncedMinUID", 0) > 1:
                return False
        return True

    def _background_asleep(self, settle: float) -> bool:
        bg = self.log("background", engine_only=True)
        if not bg:
            return False
        last = bg[-1]
        return last.msg == "Sync loop complete." and self.elapsed() - last.t >= settle

    def _foreground_idling(self) -> bool:
        fg = self.log("foreground")
        if not fg:
            return False
        if self.verbose:
            sent = [l for l in fg if l.imap_dir == "sent"]
            return bool(sent) and re.match(r"^\d+ IDLE$", sent[-1].imap_line or "") is not None
        engine = [l for l in fg if l.imap_dir is None]
        return bool(engine) and engine[-1].msg.startswith("Idling on folder")

    def wait_quiescent(self, timeout: float = 120, settle: float = 2.0, require_idle: bool = True,
                       ignore_busy: Iterable[str] = ()) -> None:
        """Block until the engine has nothing left to do. See the module docstring.
        ignore_busy: folder paths whose busy flag is known never to clear (the skipped \All
        mailbox), so they do not hold up the wait."""

        def check():
            self._drain_log()
            if not self._folders_idle(ignore_busy):
                self.wait_reason = "a folder is busy or has syncedMinUID > 1"
                return False
            if not self._background_asleep(settle):
                self.wait_reason = "background worker is not asleep"
                return False
            if require_idle and not self._foreground_idling():
                self.wait_reason = "foreground worker is not idling"
                return False
            with self._lock:
                last_delta = self.state.last_delta_t
            if self.elapsed() - last_delta < settle:
                self.wait_reason = "deltas still arriving"
                return False
            self.wait_reason = ""
            return True

        self.wait_for(check, timeout, "quiescence", poll=0.5)

    def sync_pass(self, timeout: float = 120, settle: float = 2.0, ignore_busy: Iterable[str] = ()) -> float:
        """wake-workers, then wait for the resulting background pass to finish. Returns the
        timestamp of the wake so callers can scope delta/log queries to this pass."""
        t = self.elapsed()
        self.wake()
        self.wait_for_log(r"^Marking all folders as `busy`", timeout=30, since=t)
        self.wait_quiescent(timeout=timeout, settle=settle, ignore_busy=ignore_busy)
        return t

    # -- diagnostics ----------------------------------------------------------------------

    def stderr_tail(self, n: int = 40) -> str:
        try:
            return "\n".join(self.stderr_path.read_text(errors="replace").splitlines()[-n:])
        except FileNotFoundError:
            return ""

    def describe(self) -> str:
        bg = self.log("background", engine_only=True)[-3:]
        fg = self.log("foreground", engine_only=True)[-3:]
        folders = {p: {k: v for k, v in f["localStatus"].items() if k in ("busy", "syncedMinUID", "uidnext")}
                   for p, f in self.db_folders().items()}
        return (
            f"elapsed={self.elapsed():.1f}s exit={describe_exit(self.exit_code)} deltas={len(self.state.deltas)}\n"
            f"folders={json.dumps(folders)}\n"
            f"bg tail: {[l.msg[:100] for l in bg]}\nfg tail: {[l.msg[:100] for l in fg]}\n"
            f"fg idling={self._foreground_idling()} waiting because: {self.wait_reason!r} "
            f"stdout noise={self.state.stdout_noise[-3:]}"
        )

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()
