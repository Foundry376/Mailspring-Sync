#!/usr/bin/env python3
"""Connects the real mailsync binary (--mode test) to the fake IMAP server over implicit TLS,
serving a chain from make_local_ca.sh, and checks whether mailsync accepted the certificate.

  mailsync_cert_e2e.py --mailsync <path> --ca-dir <dir> --host <127.0.0.1|localhost> \
      --expect <accept|reject> [--allow-insecure]

The leaf's only names are DNS:mailspring-test.invalid and IP:127.0.0.1, so --host localhost
reaches the same server with a name the certificate doesn't cover. "accept" means the IMAP
step got past the certificate check; the run still fails later at SMTP, which nothing serves.
"""
import argparse
import json
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fakeimap.server import FakeImapServer  # noqa: E402


def launchable_copy(binary: Path, work: Path) -> Path:
    # Release builds refuse to start unless the executable path contains "mailspring", and on
    # Windows mailsync.exe needs the DLLs beside it, so copy its whole directory.
    target = work / "mailspring-e2e"
    shutil.copytree(binary.parent, target)
    return target / binary.name


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mailsync", required=True, type=Path)
    parser.add_argument("--ca-dir", required=True, type=Path)
    parser.add_argument("--host", required=True)
    parser.add_argument("--expect", required=True, choices=["accept", "reject"])
    parser.add_argument("--allow-insecure", action="store_true")
    args = parser.parse_args()

    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(str(args.ca_dir / "chain.pem"), str(args.ca_dir / "leaf.key"))

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        binary = launchable_copy(args.mailsync.resolve(), work)
        config = work / "config"
        config.mkdir()

        server = FakeImapServer(host="127.0.0.1", port=0, tls_context=tls, implicit_tls=True)
        server.start()
        try:
            account = {
                "id": "c0ffee-cert-e2e",
                "__cls": "Account",
                "provider": "imap",
                "emailAddress": "test@example.test",
                "name": "Certificate Test",
                "settings": {
                    "imap_host": args.host,
                    "imap_port": server.port,
                    "imap_username": "test",
                    "imap_password": "pass",
                    "imap_security": "SSL / TLS",
                    "imap_allow_insecure_ssl": args.allow_insecure,
                    "smtp_host": "127.0.0.1",
                    "smtp_port": 9,
                    "smtp_username": "test",
                    "smtp_password": "pass",
                    "smtp_security": "none",
                    "smtp_allow_insecure_ssl": False,
                    "container_folder": "",
                    "create_helper_folders": False,
                },
                "aliases": [],
                "label": "test@example.test",
            }
            env = dict(os.environ, CONFIG_DIR_PATH=str(config), IDENTITY_SERVER="http://127.0.0.1:9/unreachable")
            run = subprocess.run(
                [str(binary), "--mode", "test", "--identity", "null", "--account", json.dumps(account)],
                env=env, capture_output=True, text=True, timeout=180,
            )
        finally:
            server.stop()

    start = run.stdout.find("{")
    if start < 0:
        print(f"FAIL no JSON from mailsync (exit {run.returncode}): {run.stdout[-500:]} {run.stderr[-500:]}")
        return 1
    result = json.loads(run.stdout[start:])
    if result.get("error") == "ErrorCertificate":
        got = "reject"
    elif result.get("error") is None or result.get("error_service") != "imap":
        got = "accept"
    else:
        got = "IMAP failed for another reason"
    summary = f"host={args.host} insecure={args.allow_insecure} expect={args.expect} got={got} " \
              f"(error={result.get('error')} service={result.get('error_service')})"
    if got != args.expect:
        print(f"FAIL {summary}")
        print(result.get("log", "")[-3000:])
        return 1
    print(f"OK   {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
