"""
Deterministic RFC 5322 message generation for populating servers.

Every generated message has a unique Message-ID unless a scenario asks for a duplicate:
mailsync derives a message's identity from its headers, so two different messages that
share a Message-ID, subject, date and recipients collapse onto one row. That is a real
phenomenon worth testing deliberately, never something a generator should do by accident.
"""
import email.utils
import hashlib
from datetime import datetime, timedelta, timezone
from typing import Iterable, Optional

# Recent by default: the engine only prefetches bodies for messages newer than a few days,
# and body fetching is part of what a sync scenario should exercise.
BASE = datetime.now(timezone.utc).replace(microsecond=0) - timedelta(days=3)

SENDERS = [
    ("Alice Example", "alice@example.org"),
    ("Bob Builder", "bob@example.net"),
    ("Carol Notifications", "noreply@service.example"),
    ("Dave Dev", "dave@dev.example"),
]


def message(
    n: int,
    *,
    domain: str = "example.test",
    to: str = "Harness Test <test@example.test>",
    sender: Optional[tuple] = None,
    subject: Optional[str] = None,
    date: Optional[datetime] = None,
    body: Optional[str] = None,
    in_reply_to: Optional[str] = None,
    references: Iterable[str] = (),
    html: bool = False,
    attachment: Optional[tuple] = None,   # (filename, bytes)
    calendar: bool = False,
    extra_headers: Optional[dict] = None,
    message_id: Optional[str] = None,
    age_days: Optional[float] = None,   # push the Date back, e.g. beyond the body-prefetch window
) -> bytes:
    """Message number n of a series. Deterministic for the same arguments."""
    name, addr = sender or SENDERS[n % len(SENDERS)]
    date = date or (BASE + timedelta(minutes=3 * (n % 1000)) + timedelta(hours=n // 1000))
    if age_days:
        date = date - timedelta(days=age_days)
    subject = subject if subject is not None else f"Message {n}"
    mid = message_id or f"<harness-{n}-{hashlib.sha1(subject.encode()).hexdigest()[:8]}@{domain}>"
    body = body if body is not None else f"This is the body of message {n}.\r\n\r\n-- \r\n{name}\r\n"
    headers = [
        ("Return-Path", f"<{addr}>"),
        ("Delivered-To", to.split("<")[-1].rstrip(">") if "<" in to else to),
        ("Message-ID", mid),
        ("Date", email.utils.format_datetime(date)),
        ("From", f"{name} <{addr}>"),
        ("To", to),
        ("Subject", subject),
        ("MIME-Version", "1.0"),
    ]
    if in_reply_to:
        headers.append(("In-Reply-To", in_reply_to))
    if references:
        headers.append(("References", " ".join(references)))
    for k, v in (extra_headers or {}).items():
        headers.append((k, v))

    def hdr_block(hs):
        return "".join(f"{k}: {v}\r\n" for k, v in hs)

    if not html and not attachment and not calendar:
        headers.append(("Content-Type", 'text/plain; charset="utf-8"'))
        headers.append(("Content-Transfer-Encoding", "7bit"))
        return (hdr_block(headers) + "\r\n" + body).encode("utf-8")

    boundary = f"=_harness_{hashlib.md5(mid.encode()).hexdigest()[:12]}"
    parts = []
    if html:
        inner = f"=_alt_{hashlib.md5(mid.encode()).hexdigest()[12:20]}"
        alt = (
            f"--{inner}\r\nContent-Type: text/plain; charset=\"utf-8\"\r\nContent-Transfer-Encoding: 7bit\r\n\r\n{body}\r\n"
            f"--{inner}\r\nContent-Type: text/html; charset=\"utf-8\"\r\nContent-Transfer-Encoding: 7bit\r\n\r\n"
            f"<html><body><p>{body.strip()}</p></body></html>\r\n--{inner}--\r\n"
        )
        parts.append(f"Content-Type: multipart/alternative; boundary=\"{inner}\"\r\n\r\n{alt}")
    else:
        parts.append(f"Content-Type: text/plain; charset=\"utf-8\"\r\nContent-Transfer-Encoding: 7bit\r\n\r\n{body}\r\n")
    if calendar:
        ics = ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//harness//EN\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\n"
               f"UID:{mid.strip('<>')}\r\nDTSTAMP:20260105T090000Z\r\nDTSTART:20260106T150000Z\r\nDTEND:20260106T160000Z\r\n"
               f"SUMMARY:{subject}\r\nORGANIZER:mailto:{addr}\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        parts.append(f"Content-Type: text/calendar; charset=\"utf-8\"; method=REQUEST\r\nContent-Transfer-Encoding: 7bit\r\n\r\n{ics}")
    if attachment:
        import base64
        fname, data = attachment
        if isinstance(data, str):
            data = data.encode()
        b64 = base64.encodebytes(data).decode().replace("\n", "\r\n")
        parts.append(f"Content-Type: application/octet-stream; name=\"{fname}\"\r\nContent-Transfer-Encoding: base64\r\n"
                     f"Content-Disposition: attachment; filename=\"{fname}\"\r\n\r\n{b64}")
    headers.append(("Content-Type", f'multipart/mixed; boundary="{boundary}"'))
    out = hdr_block(headers) + "\r\n"
    for p in parts:
        out += f"--{boundary}\r\n{p}"
    out += f"--{boundary}--\r\n"
    return out.encode("utf-8")


def messages(count: int, start: int = 1, **kw) -> list:
    return [message(i, **kw) for i in range(start, start + count)]


def thread(n: int, replies: int, **kw) -> list:
    """A root message and `replies` replies, each referencing the ones before it."""
    root = message(n, **kw)
    root_id = _mid(root)
    out = [root]
    refs = [root_id]
    for i in range(1, replies + 1):
        r = message(n * 1000 + i, subject=f"Re: Message {n}", in_reply_to=refs[-1], references=refs, **kw)
        out.append(r)
        refs.append(_mid(r))
    return out


def self_addressed(n: int, me: str = "Harness Test <test@example.test>", **kw) -> bytes:
    """Mail the user sent to themself: delivered to Inbox by SMTP and saved to Sent by the
    client, so the same bytes legitimately live in two folders."""
    return message(n, sender=("Harness Test", me.split("<")[-1].rstrip(">")), to=me, **kw)


def _mid(raw: bytes) -> str:
    for line in raw.split(b"\r\n"):
        if line.lower().startswith(b"message-id:"):
            return line.split(b":", 1)[1].strip().decode()
    raise ValueError("no Message-ID")


def message_id(raw: bytes) -> str:
    return _mid(raw)
