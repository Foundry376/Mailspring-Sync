#!/usr/bin/env python3
"""
A standalone Cyrus IMAP server with a persistent test account, for pointing the Mailspring
client at by hand (test/README.md, "Live Cyrus server").

    python3 test/tools/cyrus_server.py start [--imap-port 1143] [--smtp-port 1025]
    python3 test/tools/cyrus_server.py status
    python3 test/tools/cyrus_server.py stop        # keeps the mailbox; `start` resumes it
    python3 test/tools/cyrus_server.py rm          # deletes the container and its mail

`start` creates the container the first time (same image and config as the harness's
cyrus:fastmail profile), creates INBOX plus Sent, Drafts, Trash, Archive and Junk with
SPECIAL-USE, and seeds 20 messages, 3 of them self-addressed with a copy in both INBOX and
Sent. It also starts the harness's SMTP sink as a detached process: Mailspring's account
setup verifies SMTP, and mail sent to the account's own address is delivered to the Cyrus
INBOX over IMAP, as Fastmail delivers self-addressed mail. The container persists across
`stop`/`start`; the SMTP sink is restarted by `start`.
"""
import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TEST_DIR))

from harness import mailgen  # noqa: E402
from harness.servers.cyrus import CyrusServer  # noqa: E402

NAME = "mailsync-cyrus-live"
EMAIL = "test@example.test"
STATE_DIR = TEST_DIR / "runs" / "cyrus-live"
SMTP_PID = STATE_DIR / "smtp.pid"


def _container_state() -> str:
    r = subprocess.run(["docker", "inspect", "-f", "{{.State.Status}}", NAME], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def _attached(port: int) -> CyrusServer:
    s = CyrusServer("fastmail", port=port, name=NAME)
    s.container = NAME
    return s


def _seed(s: CyrusServer):
    n = 1
    inbox = []
    for i in range(14):
        inbox.append(mailgen.message(n, age_days=14 - i))
        n += 1
    s.populate("INBOX", inbox[:9], ["\\Seen"])
    s.populate("INBOX", inbox[9:], [])
    s.set_flags("INBOX", [2, 5], add=["\\Flagged"])
    s.populate("Archive", [mailgen.message(n + i, age_days=30 + i) for i in range(3)], ["\\Seen"])
    n += 3
    # Self-addressed: the MTA delivers one copy to INBOX, the client saves one to Sent.
    selfies = [mailgen.self_addressed(n + i) for i in range(3)]
    s.populate("INBOX", selfies, [])
    s.populate("Sent", selfies, ["\\Seen"])


def _start_smtp(imap_port: int, smtp_port: int):
    _stop_smtp()
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    log = open(STATE_DIR / "smtp.log", "ab")
    p = subprocess.Popen([sys.executable, __file__, "smtp", "--imap-port", str(imap_port), "--smtp-port", str(smtp_port)],
                         stdout=log, stderr=log, stdin=subprocess.DEVNULL, start_new_session=True)
    SMTP_PID.write_text(str(p.pid))


def _stop_smtp():
    if SMTP_PID.exists():
        try:
            os.kill(int(SMTP_PID.read_text()), signal.SIGTERM)
        except (ProcessLookupError, ValueError):
            pass
        SMTP_PID.unlink()


def _smtp_running() -> bool:
    try:
        os.kill(int(SMTP_PID.read_text()), 0)
        return True
    except (FileNotFoundError, ProcessLookupError, ValueError):
        return False


def cmd_start(a):
    state = _container_state()
    if state == "":
        s = CyrusServer("fastmail", port=a.imap_port, name=NAME)
        s.start()
        _seed(s)
        print(f"created {NAME} and seeded the test account")
    else:
        if state != "running":
            subprocess.run(["docker", "start", NAME], check=True, capture_output=True)
        _attached(a.imap_port)._wait_ready()
        print(f"{NAME} is running")
    _start_smtp(a.imap_port, a.smtp_port)
    cmd_status(a)


def cmd_status(a):
    print(f"container: {NAME} ({_container_state() or 'absent'})")
    print(f"IMAP: 127.0.0.1:{a.imap_port}, no TLS, user test / password pass (admin: cyrus / admin)")
    print(f"SMTP: 127.0.0.1:{a.smtp_port}, no TLS, user test / password pass ({'running' if _smtp_running() else 'stopped'})")
    if _container_state() == "running":
        s = _attached(a.imap_port)
        c = s._client()
        try:
            for line in c.list()[1]:
                print("  LIST", line.decode())
        finally:
            c.logout()


def cmd_stop(a):
    _stop_smtp()
    subprocess.run(["docker", "stop", NAME], capture_output=True)
    print(f"stopped {NAME} (mail kept)")


def cmd_rm(a):
    _stop_smtp()
    subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)
    print(f"removed {NAME}")


def cmd_smtp(a):
    from fakeimap.smtp import FakeSmtpServer
    s = _attached(a.imap_port)
    smtp = FakeSmtpServer(port=a.smtp_port, deliver_to=(s, "INBOX", EMAIL)).start()
    print(f"SMTP sink on 127.0.0.1:{smtp.port}, delivering mail for {EMAIL} to Cyrus INBOX", flush=True)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    while True:
        time.sleep(3600)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["start", "status", "stop", "rm", "smtp"])
    ap.add_argument("--imap-port", type=int, default=1143)
    ap.add_argument("--smtp-port", type=int, default=1025)
    a = ap.parse_args()
    {"start": cmd_start, "status": cmd_status, "stop": cmd_stop, "rm": cmd_rm, "smtp": cmd_smtp}[a.command](a)


if __name__ == "__main__":
    main()
