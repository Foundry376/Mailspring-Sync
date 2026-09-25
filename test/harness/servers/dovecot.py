"""
Dovecot 2.3 behind the Server interface, either as a Docker container (dovecot/dovecot
image) or a locally installed `dovecot` binary run with a private config (what an agent
container without Docker has after `apt install dovecot-imapd`).

Profiles select the advertised capabilities and folder layout:
  qresync       CONDSTORE+QRESYNC, SPECIAL-USE folders (the default Dovecot install)
  plain         imap_capability override without CONDSTORE/QRESYNC: the deep-scan branch
  proton-like   plain + an \\All "All Mail" mailbox, as PR #137 tested against
  tls           qresync over implicit TLS with a self-signed certificate

Population and mutation go over IMAP (imaplib), i.e. exactly what another client would do;
UID-space manipulation uses doveadm.
"""
import hashlib
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Optional

from .base import truth_via_imap
from .imap_client import ImapClientServer

HERE = Path(__file__).resolve().parent
TEMPLATE = (HERE.parents[1] / "servers" / "dovecot" / "dovecot.conf.tmpl").read_text()
DOCKERFILE_DIR = HERE.parents[1] / "servers" / "dovecot"


def image_tag() -> str:
    """Tagged by the build context's content so a change to it rebuilds the image."""
    if os.environ.get("HARNESS_DOVECOT_IMAGE"):
        return os.environ["HARNESS_DOVECOT_IMAGE"]
    h = hashlib.sha1()
    for p in sorted(DOCKERFILE_DIR.iterdir()):
        h.update(p.name.encode() + p.read_bytes())
    return f"mailsync-harness-dovecot:2.3.21-{h.hexdigest()[:8]}"


def ensure_image() -> str:
    """Build the harness image once per machine (a few seconds from Alpine's package)."""
    tag = image_tag()
    if os.environ.get("HARNESS_DOVECOT_IMAGE"):
        return tag
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        subprocess.run(["docker", "build", "-q", "-t", tag, str(DOCKERFILE_DIR)], check=True, capture_output=True)
    return tag


# Dovecot's full 2.3 capability list minus CONDSTORE/QRESYNC, for the plain profiles. Kept
# identical to what the #140 session used for its control run.
PLAIN_CAPS = ("IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE SORT MULTIAPPEND UNSELECT CHILDREN "
              "NAMESPACE UIDPLUS LIST-EXTENDED ESEARCH SEARCHRES WITHIN LIST-STATUS BINARY MOVE LITERAL+ SPECIAL-USE")

SPECIAL_USE = {
    "Drafts": "\\Drafts", "Sent": "\\Sent", "Junk": "\\Junk", "Trash": "\\Trash", "Archive": "\\Archive",
}

PROFILES = {
    "qresync": dict(capability=None, mailboxes=SPECIAL_USE, ssl=False, format="maildir"),
    "plain": dict(capability=PLAIN_CAPS, mailboxes=SPECIAL_USE, ssl=False, format="maildir"),
    "proton-like": dict(capability=PLAIN_CAPS, mailboxes={**SPECIAL_USE, "All Mail": "\\All"}, ssl=False, format="maildir"),
    "tls": dict(capability=None, mailboxes=SPECIAL_USE, ssl=True, format="maildir"),
    "sdbox": dict(capability=None, mailboxes=SPECIAL_USE, ssl=False, format="sdbox"),
}


def docker_available() -> bool:
    try:
        return subprocess.run(["docker", "info"], capture_output=True, timeout=10).returncode == 0
    except Exception:
        return False


def local_dovecot_available() -> bool:
    return shutil.which("dovecot") is not None and shutil.which("doveadm") is not None


def default_mode() -> str:
    return os.environ.get("HARNESS_DOVECOT_MODE") or ("local" if local_dovecot_available() else "docker")


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class DovecotServer(ImapClientServer):
    kind = "dovecot"

    def __init__(self, profile: str = "qresync", mode: Optional[str] = None, work_dir: Optional[str] = None,
                 log_path: Optional[str] = None):
        if profile not in PROFILES:
            raise ValueError(f"unknown dovecot profile {profile}; known: {sorted(PROFILES)}")
        self.profile = profile
        self.cfg = PROFILES[profile]
        self.mode = mode or default_mode()
        self.work = Path(work_dir or tempfile.mkdtemp(prefix="harness-dovecot-"))
        self.container: Optional[str] = None
        self.proc: Optional[subprocess.Popen] = None
        self.ssl = self.cfg["ssl"]
        self.port = 0

    # -- config ---------------------------------------------------------------------------------

    def _render(self, base_dir: str, mail_dir: str, uid: int, gid: int, port: int, cert_dir: Optional[str]) -> str:
        blocks = "".join(
            f"  mailbox \"{name}\" {{\n    special_use = {flag}\n    auto = subscribe\n  }}\n"
            for name, flag in self.cfg["mailboxes"].items()
        )
        cap = f"  imap_capability = {self.cfg['capability']}" if self.cfg["capability"] else ""
        ssl_lines = f"ssl_cert = <{cert_dir}/cert.pem\nssl_key = <{cert_dir}/key.pem" if self.ssl else ""
        fmt = os.environ.get("HARNESS_DOVECOT_FORMAT", self.cfg.get("format", "maildir"))
        location = f"maildir:{mail_dir}/Maildir" if fmt == "maildir" else f"{fmt}:{mail_dir}/{fmt}"
        return TEMPLATE.format(base_dir=base_dir, mail_dir=mail_dir, uid=uid, gid=gid, port=port, mail_location=location,
                               password=self.password, capability_line=cap, mailbox_blocks=blocks,
                               ssl="yes" if self.ssl else "no", ssl_lines=ssl_lines)

    # -- lifecycle ------------------------------------------------------------------------------

    def start(self):
        self.work.mkdir(parents=True, exist_ok=True)
        if self.mode == "docker":
            self._start_docker()
        else:
            self._start_local()
        self._wait_ready()
        return self

    def _start_docker(self):
        # Everything lives inside the container; only the rendered config is copied in, so
        # host filesystem permissions never matter.
        conf = self.work / "dovecot.conf"
        conf.write_text(self._render("/tmp/dovecot", "/srv/mail/test", 1000, 1000, 143,
                                     "/tmp/dovecot" if self.ssl else None))
        image = ensure_image()
        self.port = _free_port()
        name = f"harness-dovecot-{os.getpid()}-{int(time.time() * 1000) % 100000}"
        subprocess.run(["docker", "create", "--name", name, "-p", f"127.0.0.1:{self.port}:143",
                        "--entrypoint", "sh", image, "-c",
                        "mkdir -p /tmp/dovecot /srv/mail/test && chown 1000:1000 /srv/mail/test && "
                        + ("openssl req -x509 -newkey rsa:2048 -nodes -keyout /tmp/dovecot/key.pem -out /tmp/dovecot/cert.pem -days 2 -subj /CN=localhost 2>/dev/null && " if self.ssl else "")
                        + "exec dovecot -c /tmp/harness.conf -F"],
                       check=True, capture_output=True)
        self.container = name
        subprocess.run(["docker", "cp", str(conf), f"{name}:/tmp/harness.conf"], check=True, capture_output=True)
        subprocess.run(["docker", "start", name], check=True, capture_output=True)

    def _start_local(self):
        base = self.work / "dovecot"
        mail = self.work / "mail"
        base.mkdir(exist_ok=True)
        mail.mkdir(exist_ok=True)
        uid, gid = os.getuid(), os.getgid()
        if uid == 0:
            # Dovecot refuses to touch mail as root; use (and create if needed) a system user.
            import pwd
            try:
                pw = pwd.getpwnam("harness")
            except KeyError:
                subprocess.run(["useradd", "-M", "-s", "/bin/false", "harness"], check=True)
                pw = pwd.getpwnam("harness")
            uid, gid = pw.pw_uid, pw.pw_gid
            os.chown(mail, uid, gid)
        self.port = _free_port()
        cert_dir = None
        if self.ssl:
            cert_dir = str(base)
            _make_self_signed(base)
        conf = base / "dovecot.conf"
        conf.write_text(self._render(str(base), str(mail), uid, gid, self.port, cert_dir))
        self.conf_path = conf
        self.proc = subprocess.Popen(["dovecot", "-c", str(conf), "-F"], stdout=subprocess.DEVNULL,
                                     stderr=open(base / "stderr.log", "wb"), start_new_session=True)

    def _wait_ready(self, timeout: float = 60):
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=2) as s:
                    if self.ssl:
                        return
                    if b"OK" in s.recv(200):
                        return
            except OSError as e:
                last = e
            time.sleep(0.3)
        raise RuntimeError(f"dovecot did not become ready on {self.port}: {last}\n{self.logs()[-2000:]}")

    def stop(self):
        if self.container:
            subprocess.run(["docker", "logs", self.container], capture_output=True)
            subprocess.run(["docker", "rm", "-f", self.container], capture_output=True)
            self.container = None
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None

    def logs(self) -> str:
        if self.container:
            r = subprocess.run(["docker", "exec", self.container, "cat", "/tmp/dovecot/dovecot.log"], capture_output=True, text=True)
            return r.stdout
        p = self.work / "dovecot" / "dovecot.log"
        return p.read_text() if p.exists() else ""

    # -- doveadm ----------------------------------------------------------------------------------

    def doveadm(self, *args: str) -> str:
        if self.container:
            cmd = ["docker", "exec", self.container, "doveadm", "-c", "/tmp/harness.conf", *args]
        else:
            cmd = ["doveadm", "-c", str(self.conf_path), *args]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"doveadm {' '.join(args)} failed: {r.stderr.strip()}")
        return r.stdout

    # -- IMAP ------------------------------------------------------------------------------------

    def account_kwargs(self) -> dict:
        kw = super().account_kwargs()
        if self.ssl:
            kw["imap_security"] = "SSL / TLS"
        return kw

    def truth(self):
        return truth_via_imap(self.host, self.port, self.username, self.password, ssl=self.ssl)

    # special_use on create_mailbox would need a config change; scenarios that need a role on
    # a new folder should list it in the profile instead.

    def set_uidvalidity(self, mailbox, value):
        self.doveadm("mailbox", "update", "-u", self.username, "--uid-validity", str(value), mailbox)

    def set_uidnext(self, mailbox, value):
        self.doveadm("mailbox", "update", "-u", self.username, "--min-next-uid", str(value), mailbox)

    def drop_connections(self):
        self.doveadm("kick", self.username)


def _make_self_signed(dir_: Path):
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(dir_ / "key.pem"),
                    "-out", str(dir_ / "cert.pem"), "-days", "2", "-subj", "/CN=localhost"],
                   check=True, capture_output=True)
