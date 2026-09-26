"""
Cyrus IMAP 3.6 (Debian bookworm's cyrus-imapd) behind the Server interface, one Docker
container per run, built from test/servers/cyrus/. Cyrus is what Fastmail runs: its own
CONDSTORE/QRESYNC, MOVE/COPYUID and SPECIAL-USE implementations, and delayed expunge.

Profiles:
  fastmail     (default) Fastmail's layout: NAMESPACE (("" "/")) - altnamespace and
               unixhierarchysep on, personal folders at the top level - with CONDSTORE and
               QRESYNC (always on in Cyrus 3). Scenario names are server names.
  default-ns   Debian's out-of-the-box layout: folders under "INBOX." with "." as the
               separator. No major provider is known to use it; it covers the engine's
               namespace-prefix handling.
  plain        fastmail + suppress_capabilities CONDSTORE QRESYNC: the deep-scan branch.

Namespace. Under default-ns the server spells a scenario's `Archive` as `INBOX.Archive`
(and `a/b` as `INBOX.a.b`). The adapter maps scenario names to server paths on every
operation and maps server paths - LIST output here, `Folder.path` in the engine's database
via ScenarioRun._named - back to scenario names, so scenarios, expectations and reports keep
the names they use on every other server kind, while the engine sees exactly what a Cyrus
user's client sees. Under the other profiles both mappings are the identity.

Population and mutation go over IMAP as the test user; the user, ACL-free admin work and
UID-space manipulation use the `cyrus` admin over IMAP and Cyrus's tools in the container:
  set_uidvalidity  drop the mailbox's cyrus.index/cache and `reconstruct` it, which assigns
                   a fresh (time-based) UIDVALIDITY and keeps UIDs; flags are restored over
                   IMAP and connections dropped afterwards. The requested value itself
                   cannot be chosen.
  set_uidnext      drop a message file named `<value-1>.` into the spool, `reconstruct -f`
                   to index it, then expunge it: UIDNEXT becomes `value`.
  drop_connections SIGTERM every imapd; the master forks fresh ones.
The server transcript is Cyrus's per-user telemetry log (/var/lib/cyrus/log/<user>/),
collected into the run's server.log on stop; it includes the harness's own sessions.
"""
import hashlib
import imaplib
import os
import socket
import subprocess
import time
from pathlib import Path
from typing import Optional

from .base import truth_via_imap
from .imap_client import ImapClientServer, uid_set

HERE = Path(__file__).resolve().parent
DOCKERFILE_DIR = HERE.parents[1] / "servers" / "cyrus"
ADMIN, ADMIN_PASSWORD = "cyrus", "admin"

SPECIAL_USE = {
    "Drafts": "\\Drafts", "Sent": "\\Sent", "Junk": "\\Junk", "Trash": "\\Trash", "Archive": "\\Archive",
}

PROFILES = {
    "fastmail": dict(conf="", altnamespace=True),
    "default-ns": dict(conf="altnamespace: no\nunixhierarchysep: no", altnamespace=False),
    "plain": dict(conf="suppress_capabilities: CONDSTORE QRESYNC", altnamespace=True),
}


def image_tag() -> str:
    """Tagged by the build context's content so a config change rebuilds the image."""
    if os.environ.get("HARNESS_CYRUS_IMAGE"):
        return os.environ["HARNESS_CYRUS_IMAGE"]
    h = hashlib.sha1()
    for p in sorted(DOCKERFILE_DIR.iterdir()):
        h.update(p.name.encode() + p.read_bytes())
    return f"mailsync-harness-cyrus:3.6-{h.hexdigest()[:8]}"


def ensure_image() -> str:
    tag = image_tag()
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        subprocess.run(["docker", "build", "-q", "-t", tag, str(DOCKERFILE_DIR)], check=True, capture_output=True)
    return tag


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class CyrusServer(ImapClientServer):
    kind = "cyrus"

    def __init__(self, profile: str = "fastmail", log_path: Optional[str] = None, port: int = 0,
                 name: Optional[str] = None):
        if profile not in PROFILES:
            raise ValueError(f"unknown cyrus profile {profile}; known: {sorted(PROFILES)}")
        self.profile = profile
        self.cfg = PROFILES[profile]
        self.log_path = log_path
        self.port = port
        self.container_name = name
        self.container: Optional[str] = None

    # -- namespace ------------------------------------------------------------------------------

    def server_path(self, name: str) -> str:
        if self.cfg["altnamespace"] or name.upper() == "INBOX" or name.startswith("INBOX."):
            return name
        return "INBOX." + name.replace("/", ".")

    def scenario_name(self, path: str) -> str:
        if not self.cfg["altnamespace"] and path.startswith("INBOX."):
            return path[len("INBOX."):].replace(".", "/")
        return path

    def _admin_name(self, name: str) -> str:
        """The mailbox as the admin names it (what CREATE as admin, mbpath and reconstruct take)."""
        sep = "/" if self.cfg["altnamespace"] else "."
        path = self.server_path(name)
        if path.upper() == "INBOX":
            return f"user{sep}{self.username}"
        rest = path[len("INBOX."):] if path.startswith("INBOX.") else path
        return f"user{sep}{self.username}{sep}{rest}"

    # -- lifecycle ------------------------------------------------------------------------------

    def start(self):
        tag = ensure_image()
        self.port = self.port or _free_port()
        name = self.container_name or f"harness-cyrus-{os.getpid()}-{int(time.time() * 1000) % 100000}"
        env = ["-e", f"CYRUS_USERS={ADMIN}:{ADMIN_PASSWORD} {self.username}:{self.password}"]
        if self.cfg["conf"]:
            env += ["-e", f"CYRUS_EXTRA_CONF={self.cfg['conf']}"]
        subprocess.run(["docker", "run", "-d", "--name", name, "-p", f"127.0.0.1:{self.port}:143", *env, tag],
                       check=True, capture_output=True)
        self.container = name
        try:
            self._wait_ready()
            self._provision()
        except Exception:
            self.stop()
            raise
        return self

    def _wait_ready(self, timeout: float = 60):
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=2) as s:
                    if s.recv(200).startswith(b"* OK"):
                        return
            except OSError as e:
                last = e
            time.sleep(0.3)
        raise RuntimeError(f"cyrus did not become ready on {self.port}: {last}\n{self.logs()[-2000:]}")

    def _provision(self):
        # Telemetry is only written for sessions that start after the directory exists.
        self.exec(f"mkdir -p /var/lib/cyrus/log/{self.username}", user="cyrus")
        a = imaplib.IMAP4("127.0.0.1", self.port)
        try:
            a.login(ADMIN, ADMIN_PASSWORD)
            typ, data = a.create(self._admin_name("INBOX"))
            if typ != "OK":
                raise RuntimeError(f"admin CREATE {self._admin_name('INBOX')}: {data}")
        finally:
            a.logout()
        for mailbox, use in SPECIAL_USE.items():
            self.create_mailbox(mailbox, use)

    def stop(self):
        if not self.container:
            return
        if self.log_path:
            try:
                Path(self.log_path).write_text(self.transcript())
            except Exception as e:
                Path(self.log_path).write_text(f"(could not collect the Cyrus telemetry log: {e})\n")
        subprocess.run(["docker", "rm", "-f", self.container], capture_output=True)
        self.container = None

    def exec(self, script: str, user: str = "cyrus", check: bool = True) -> str:
        r = subprocess.run(["docker", "exec", "-u", user, self.container, "sh", "-c", script],
                           capture_output=True, text=True)
        if check and r.returncode != 0:
            raise RuntimeError(f"cyrus: `{script}` failed ({r.returncode}): {r.stderr.strip() or r.stdout.strip()}")
        return r.stdout

    def logs(self) -> str:
        if not self.container:
            return ""
        return subprocess.run(["docker", "logs", self.container], capture_output=True, text=True).stdout

    def transcript(self) -> str:
        """Every session's telemetry file, oldest first, each under a header naming its pid."""
        return self.exec(f"cd /var/lib/cyrus/log/{self.username} && for f in $(ls -tr); do "
                         "echo \"===== $f\"; cat \"$f\"; done", check=False)

    # -- IMAP -----------------------------------------------------------------------------------

    def truth(self):
        t = truth_via_imap(self.host, self.port, self.username, self.password)
        return {self.scenario_name(path): v for path, v in t.items()}

    def create_mailbox(self, name, special_use=None):
        if not special_use:
            return super().create_mailbox(name)
        c = self._client()
        try:
            typ, data = c._simple_command("CREATE", self._qm(name), f"(USE ({special_use}))")
            if typ != "OK" and b"exists" not in (data[0] or b"").lower():
                raise RuntimeError(f"CREATE {name} USE {special_use}: {data}")
            c.subscribe(self._qm(name))
        finally:
            c.logout()

    def _status(self, mailbox: str) -> dict:
        c = self._client()
        try:
            typ, data = c.status(self._qm(mailbox), "(UIDVALIDITY UIDNEXT)")
        finally:
            c.logout()
        words = data[0].decode().rsplit("(", 1)[1].rstrip(")").split()
        return {words[i]: int(words[i + 1]) for i in range(0, len(words), 2)}

    def _flags_by_uid(self, mailbox: str) -> dict:
        def go(c):
            typ, data = c.uid("FETCH", "1:*", "(UID FLAGS)")
            out = {}
            for line in data or []:
                if not isinstance(line, bytes) or b"UID" not in line:
                    continue
                text = line.decode()
                uid = int(text.split("UID ", 1)[1].split()[0].rstrip(")"))
                flags = text.split("FLAGS (", 1)[1].split(")", 1)[0].split()
                out[uid] = [f for f in flags if f != "\\Recent"]
            return out
        return self._with_selected(mailbox, go)

    # -- UID space ------------------------------------------------------------------------------

    def _spool(self, mailbox: str) -> str:
        return self.exec(f"/usr/lib/cyrus/bin/mbpath {self._admin_name(mailbox)}").strip()

    def set_uidvalidity(self, mailbox, value):
        before = self._status(mailbox)["UIDVALIDITY"]
        flags = self._flags_by_uid(mailbox)
        spool = self._spool(mailbox)
        # Delayed expunge leaves expunged messages' files in the spool; a rebuilt index
        # would bring them back, so only the live UIDs' files survive.
        live = " ".join(f"{u}." for u in flags) or "-"
        for _ in range(5):
            self.exec(f"cd {spool} && for f in [0-9]*.; do case \" {live} \" in *\" $f \"*) ;; *) rm -f \"$f\";; esac; done; "
                      f"rm -f cyrus.index cyrus.cache && /usr/lib/cyrus/bin/reconstruct {self._admin_name(mailbox)}")
            if self._status(mailbox)["UIDVALIDITY"] != before:
                break
            time.sleep(1.1)   # UIDVALIDITY is time-based; same second, same value
        else:
            raise RuntimeError(f"reconstruct did not change the UIDVALIDITY of {mailbox}")
        by_flags: dict = {}
        for uid, fl in flags.items():
            if fl:
                by_flags.setdefault(tuple(sorted(fl)), []).append(uid)

        def restore(c):
            for fl, uids in by_flags.items():
                c.uid("STORE", uid_set(uids), "+FLAGS.SILENT", "(" + " ".join(fl) + ")")
        if by_flags:
            self._with_selected(mailbox, restore)
        # A session that had the mailbox selected keeps the old index open and answers every
        # command with "NO Mailbox does not exist" until it re-SELECTs. A rebuilt mailbox
        # comes with a restart in practice, and Dovecot's `mailbox update` drops the session
        # too, so the harness does the same.
        self.drop_connections()

    def set_uidnext(self, mailbox, value):
        if self._status(mailbox)["UIDNEXT"] >= value:
            return
        placeholder = value - 1
        spool = self._spool(mailbox)
        self.exec(f"printf 'Message-ID: <uidnext-placeholder-{placeholder}@harness>\\r\\nSubject: placeholder\\r\\n\\r\\nx\\r\\n' "
                  f"> {spool}/{placeholder}. && /usr/lib/cyrus/bin/reconstruct -f {self._admin_name(mailbox)}")
        self.expunge(mailbox, [placeholder])

    def drop_connections(self):
        self.exec("pkill -TERM -x imapd", user="root", check=False)
