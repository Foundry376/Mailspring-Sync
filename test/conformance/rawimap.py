"""A minimal raw IMAP client that returns the server's exact response lines, literals
inlined, so two servers' answers to the same command can be compared byte for byte."""
import re
import socket
import ssl as _ssl
from typing import Optional


class RawImap:
    def __init__(self, host: str, port: int, ssl: bool = False, timeout: float = 30):
        sock = socket.create_connection((host, port), timeout=timeout)
        if ssl:
            ctx = _ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = _ssl.CERT_NONE
            sock = ctx.wrap_socket(sock)
        self.sock = sock
        self.f = sock.makefile("rb")
        self.n = 0
        self.greeting = self._line()
        self.log: list = []

    def _line(self) -> bytes:
        line = self.f.readline()
        if not line:
            raise EOFError("connection closed")
        return line

    def _read_response_line(self) -> bytes:
        """One logical response line: literals ({n}\\r\\n + n bytes) are appended inline."""
        out = b""
        line = self._line()
        while True:
            out += line
            m = re.search(rb"\{(\d+)\}\r\n$", line)
            if not m:
                return out
            n = int(m.group(1))
            data = b""
            while len(data) < n:
                chunk = self.f.read(n - len(data))
                if not chunk:
                    raise EOFError
                data += chunk
            out += data
            line = self._line()

    def cmd(self, command: str, literal: Optional[bytes] = None) -> list:
        """Send a command; returns [untagged..., tagged] as bytes lines (CRLF stripped,
        literals inlined). If `literal` is given, the command must end with {n} and the
        literal is sent after the continuation request."""
        self.n += 1
        tag = f"a{self.n:03d}".encode()
        self.sock.sendall(tag + b" " + command.encode() + b"\r\n")
        self.log.append(("C", tag + b" " + command.encode()))
        lines = []
        while True:
            line = self._read_response_line()
            self.log.append(("S", line))
            if line.startswith(b"+") and literal is not None:
                self.sock.sendall(literal + b"\r\n")
                literal = None
                continue
            lines.append(line.rstrip(b"\r\n"))
            if line.startswith(tag + b" "):
                return lines

    def idle_start(self):
        self.n += 1
        self.idle_tag = f"a{self.n:03d}".encode()
        self.sock.sendall(self.idle_tag + b" IDLE\r\n")
        line = self._line()
        assert line.startswith(b"+"), line

    def idle_collect(self, seconds: float) -> list:
        """Read untagged lines arriving during IDLE for `seconds`."""
        import select
        import time
        out = []
        deadline = time.time() + seconds
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                return out
            pending = isinstance(self.sock, _ssl.SSLSocket) and self.sock.pending()
            if not pending:
                r, _, _ = select.select([self.sock], [], [], remaining)
                if not r:
                    return out
            out.append(self._read_response_line().rstrip(b"\r\n"))

    def idle_done(self) -> list:
        self.sock.sendall(b"DONE\r\n")
        lines = []
        while True:
            line = self._read_response_line().rstrip(b"\r\n")
            lines.append(line)
            if line.startswith(self.idle_tag + b" "):
                return lines

    def close(self):
        try:
            self.cmd("LOGOUT")
        except Exception:
            pass
        self.sock.close()
