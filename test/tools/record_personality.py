#!/usr/bin/env python3
"""
Capture how a real IMAP server answers the exchanges mailsync performs, as a fixture the
fake server's personalities can be checked against (or built from).

    IMAP_PASSWORD=... python3 test/tools/record_personality.py imap.mail.me.com 993 user@icloud.com --name icloud

Writes test/fakeimap/recordings/<name>.json with the greeting, pre/post-login capabilities,
NAMESPACE, ID, LIST, STATUS/SELECT of INBOX, and the FLAGS/MODSEQ FETCH shape of the two
newest messages. Nothing message-specific is recorded: no ENVELOPE, no bodies, no subjects.
Mailbox names are recorded because the folder layout is the point; review the file before
committing it if your folder names are sensitive.
"""
import argparse
import getpass
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from conformance.rawimap import RawImap  # noqa: E402

OUT_DIR = Path(__file__).resolve().parents[1] / "fakeimap" / "recordings"


def caps(lines):
    for l in lines:
        m = re.search(rb"CAPABILITY ([^\]\r\n]+)", l)
        if m:
            return m.group(1).decode()
    return ""


def redact(line: bytes, user: str) -> str:
    s = line.decode("utf-8", "replace")
    s = s.replace(user, "<user>")
    s = re.sub(r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}", "<ip>", s)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("port", type=int)
    ap.add_argument("user")
    ap.add_argument("--name", required=True, help="fixture name, e.g. icloud, outlook, yahoo")
    ap.add_argument("--no-ssl", action="store_true")
    ap.add_argument("--xoauth2", help="access token for AUTHENTICATE XOAUTH2 instead of LOGIN")
    args = ap.parse_args()
    password = os.environ.get("IMAP_PASSWORD") or (None if args.xoauth2 else getpass.getpass("IMAP password: "))

    c = RawImap(args.host, args.port, ssl=not args.no_ssl)
    rec = {"host": args.host, "greeting": redact(c.greeting.rstrip(b"\r\n"), args.user),
           "preauth_capabilities": caps([c.greeting])}
    pre = c.cmd("CAPABILITY")
    rec["preauth_capabilities"] = rec["preauth_capabilities"] or caps([b"CAPABILITY " + l for l in pre])
    if args.xoauth2:
        import base64
        sasl = base64.b64encode(f"user={args.user}\x01auth=Bearer {args.xoauth2}\x01\x01".encode()).decode()
        login = c.cmd(f"AUTHENTICATE XOAUTH2 {sasl}")
    else:
        login = c.cmd(f'LOGIN "{args.user}" "{password}"')
    rec["login"] = [redact(l, args.user) for l in login]
    rec["postauth_capabilities"] = caps(login) or caps([b"CAPABILITY " + l for l in c.cmd("CAPABILITY")])
    rec["namespace"] = [redact(l, args.user) for l in c.cmd("NAMESPACE")]
    rec["id"] = [redact(l, args.user) for l in c.cmd('ID ("name" "Mailspring" "version" "harness")')]
    rec["enable"] = [redact(l, args.user) for l in c.cmd("ENABLE QRESYNC CONDSTORE")]
    rec["list"] = [redact(l, args.user) for l in c.cmd('LIST "" "*"')]
    if "XLIST" in rec["postauth_capabilities"]:
        rec["xlist"] = [redact(l, args.user) for l in c.cmd('XLIST "" "*"')]
    rec["status_inbox"] = [redact(l, args.user) for l in c.cmd("STATUS INBOX (UNSEEN MESSAGES RECENT UIDNEXT UIDVALIDITY HIGHESTMODSEQ)")]
    rec["select_inbox"] = [redact(l, args.user) for l in c.cmd("SELECT INBOX")]
    rec["examine_inbox"] = [redact(l, args.user) for l in c.cmd("EXAMINE INBOX")]
    exists = 0
    for l in rec["select_inbox"]:
        m = re.match(r"^\* (\d+) EXISTS", l)
        if m:
            exists = int(m.group(1))
    if exists:
        lo = max(1, exists - 1)
        items = "(UID FLAGS MODSEQ INTERNALDATE RFC822.SIZE X-GM-MSGID X-GM-THRID X-GM-LABELS)" if "X-GM-EXT-1" in rec["postauth_capabilities"] else "(UID FLAGS MODSEQ INTERNALDATE RFC822.SIZE)"
        rec["fetch_attrs"] = [redact(l, args.user) for l in c.cmd(f"FETCH {lo}:{exists} {items}")]
        hms = re.search(r"HIGHESTMODSEQ (\d+)", " ".join(rec["select_inbox"]))
        if hms:
            rec["fetch_changedsince_vanished"] = [redact(l, args.user) for l in
                                                 c.cmd(f"UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE {max(1, int(hms.group(1)) - 5)} VANISHED)")][:20]
    c.close()
    OUT_DIR.mkdir(exist_ok=True)
    out = OUT_DIR / f"{args.name}.json"
    out.write_text(json.dumps(rec, indent=2) + "\n")
    print(f"wrote {out}")
    print("pre-auth :", rec["preauth_capabilities"])
    print("post-auth:", rec["postauth_capabilities"])
    for l in rec["list"][:12]:
        print("  ", l)


if __name__ == "__main__":
    main()
