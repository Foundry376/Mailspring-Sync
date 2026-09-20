"""IMAP response formatting: quoting, literals, ENVELOPE, BODYSTRUCTURE and sections."""
import email
import email.utils
import re
from datetime import datetime, timezone
from typing import Optional

MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def internaldate(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    off = dt.utcoffset()
    total = int(off.total_seconds()) if off else 0
    sign = "+" if total >= 0 else "-"
    total = abs(total)
    return f'"{dt.day:02d}-{MONTHS[dt.month - 1]}-{dt.year} {dt:%H:%M:%S} {sign}{total // 3600:02d}{(total % 3600) // 60:02d}"'


def parse_internaldate(s: str) -> datetime:
    return datetime.strptime(s.strip('"'), "%d-%b-%Y %H:%M:%S %z")


def astring(s: Optional[str]) -> bytes:
    """Quote a string the way a server does: NIL for None, quoted when safe, literal otherwise."""
    if s is None:
        return b"NIL"
    b = s.encode("utf-8", "surrogateescape")
    if b"\r" in b or b"\n" in b or any(c >= 0x80 for c in b) or b"\0" in b:
        return b"{%d}\r\n" % len(b) + b
    return b'"' + b.replace(b"\\", b"\\\\").replace(b'"', b'\\"') + b'"'


_ATOM_SAFE = re.compile(r'^[^(){ %*"\\\]\x00-\x1f\x7f-\xff]+$')


def mailbox_name(name: str) -> bytes:
    """Dovecot writes mailbox names as bare atoms when nothing needs quoting."""
    if _ATOM_SAFE.match(name) and name.upper() != "NIL":
        return name.encode()
    return astring(name)


def nstring(s: Optional[str]) -> bytes:
    return astring(s) if s not in (None, "") else (b"NIL" if s is None else b'""')


def _addresses(value: Optional[str]) -> bytes:
    if not value:
        return b"NIL"
    parsed = email.utils.getaddresses([value])
    out = []
    for name, addr in parsed:
        if not addr and not name:
            continue
        mailbox, _, host = addr.rpartition("@")
        if not host:
            mailbox, host = addr, None
        out.append(b"(" + b" ".join([nstring(name or None), b"NIL", nstring(mailbox or None), nstring(host)]) + b")")
    return b"(" + b"".join(out) + b")" if out else b"NIL"


def envelope(msg) -> bytes:
    """ENVELOPE per RFC 3501 7.4.2, from a parsed email.message.Message."""
    h = lambda n: _hdr(msg, n)
    from_ = h("From")
    return b"(" + b" ".join([
        nstring(h("Date")),
        nstring(h("Subject")),
        _addresses(from_),
        _addresses(h("Sender") or from_),
        _addresses(h("Reply-To") or from_),
        _addresses(h("To")),
        _addresses(h("Cc")),
        _addresses(h("Bcc")),
        nstring(h("In-Reply-To")),
        nstring(h("Message-ID")),
    ]) + b")"


def _hdr(msg, name) -> Optional[str]:
    v = msg.get(name)
    if v is None:
        return None
    return str(v).replace("\r\n", " ").replace("\n", " ").strip()


def _params(items) -> bytes:
    if not items:
        return b"NIL"
    out = []
    for k, v in items:
        out += [astring(k.lower()), astring(v)]
    return b"(" + b" ".join(out) + b")"


def _part_bytes(part) -> bytes:
    """The part's body as it sits on the wire (still transfer-encoded)."""
    payload = part.get_payload(decode=False)
    if isinstance(payload, list):
        return b""
    if isinstance(payload, bytes):
        return payload
    return (payload or "").encode("utf-8", "surrogateescape")


def bodystructure(msg, extended: bool = True) -> bytes:
    if msg.is_multipart():
        parts = b"".join(bodystructure(p, extended) for p in msg.get_payload())
        subtype = astring(msg.get_content_subtype().lower())
        if not extended:
            return b"(" + parts + subtype + b")"
        params = [(k, v) for k, v in (msg.get_params() or [])[1:]]
        return b"(" + parts + subtype + b" " + _params(params) + b" NIL NIL NIL)"
    ctype = astring(msg.get_content_maintype().lower())
    subtype = astring(msg.get_content_subtype().lower())
    params = [(k, v) for k, v in (msg.get_params() or [])[1:]]
    body = _part_bytes(msg)
    size = len(body)
    fields = [ctype, subtype, _params(params), nstring(msg.get("Content-ID")),
              nstring(msg.get("Content-Description")), astring((msg.get("Content-Transfer-Encoding") or "7bit").lower()),
              str(size).encode()]
    if msg.get_content_maintype() == "text":
        fields.append(str(body.count(b"\n")).encode())
    elif msg.get_content_type() == "message/rfc822":
        inner = msg.get_payload()[0]
        fields += [envelope(inner), bodystructure(inner, extended), str(body.count(b"\n")).encode()]
    if extended:
        disp = msg.get("Content-Disposition")
        if disp:
            dparams = [(k, v) for k, v in (msg.get_params(header="content-disposition") or [])[1:]]
            dval = b"(" + astring(disp.split(";")[0].strip().lower()) + b" " + _params(dparams) + b")"
        else:
            dval = b"NIL"
        fields += [b"NIL", dval, b"NIL", b"NIL"]
    return b"(" + b" ".join(fields) + b")"


def split_header_body(raw: bytes):
    i = raw.find(b"\r\n\r\n")
    if i < 0:
        return raw, b""
    return raw[: i + 4], raw[i + 4:]


def header_fields(raw: bytes, names: list, invert: bool = False) -> bytes:
    header, _ = split_header_body(raw)
    wanted = {n.upper() for n in names}
    out = []
    current = None
    for line in header.rstrip(b"\r\n").split(b"\r\n"):
        if line[:1] in (b" ", b"\t") and current is not None:
            if current:
                out.append(line)
            continue
        name = line.split(b":", 1)[0].decode("ascii", "replace").upper()
        keep = (name in wanted) != invert
        current = keep
        if keep and b":" in line:
            out.append(line)
    return b"\r\n".join(out) + (b"\r\n\r\n" if out else b"\r\n")


def section(msg_raw: bytes, parsed, spec: str) -> bytes:
    """Resolve a BODY[...] section spec (already stripped of brackets)."""
    spec = spec.strip()
    if spec == "":
        return msg_raw
    up = spec.upper()
    if up == "HEADER":
        return split_header_body(msg_raw)[0]
    if up == "TEXT":
        return split_header_body(msg_raw)[1]
    m = re.match(r"^HEADER\.FIELDS(\.NOT)? \((.*)\)$", spec, re.I)
    if m:
        return header_fields(msg_raw, m.group(2).split(), invert=bool(m.group(1)))
    # numeric part path, optionally followed by .MIME / .HEADER / .TEXT
    m = re.match(r"^([\d.]+?)(?:\.(MIME|HEADER|TEXT|HEADER\.FIELDS(?:\.NOT)? \(.*\)))?$", spec, re.I)
    if not m:
        return b""
    part = parsed
    for idx in m.group(1).split("."):
        i = int(idx)
        if part.is_multipart():
            payload = part.get_payload()
            if i - 1 >= len(payload):
                return b""
            part = payload[i - 1]
        elif part.get_content_type() == "message/rfc822":
            part = part.get_payload()[0]
        elif i == 1:
            pass  # BODY[1] of a non-multipart message is its body
        else:
            return b""
    sub = (m.group(2) or "").upper()
    part_raw = part.as_bytes() if part is not parsed else msg_raw
    part_raw = part_raw.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    head, body = split_header_body(part_raw)
    if sub == "MIME" or sub == "HEADER":
        return head
    if sub == "TEXT":
        return body
    if sub.startswith("HEADER.FIELDS"):
        mm = re.match(r"^HEADER\.FIELDS(\.NOT)? \((.*)\)$", m.group(2), re.I)
        return header_fields(part_raw, mm.group(2).split(), invert=bool(mm.group(1)))
    return body if part is not parsed or part.is_multipart() is False else body
