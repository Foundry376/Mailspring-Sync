"""The harness's engine-log tailer across spdlog rotations. The engine rotates its log at 5 MB
and keeps three files (main.cpp); a burst that rotates twice between two drains, as the
8.8 MB MIME of graph-send-large-attachments does in --verbose mode, must lose no line."""
import os
import threading
from pathlib import Path

from harness.mailsync import MailsyncProcess, ProcessState


def tailer(tmp_path: Path) -> MailsyncProcess:
    ms = MailsyncProcess.__new__(MailsyncProcess)
    ms.account = {"id": "c0ffee"}
    ms.config_dir = tmp_path
    ms.state = ProcessState()
    ms._lock = threading.Lock()
    ms._log_lock = threading.Lock()
    ms._log_pos = 0
    ms._log_ino = None
    ms._t0 = 0.0
    return ms


def line(n: int) -> bytes:
    return f"1 [2026-10-28 09:00:00.000] [foreground] [info] line {n}\n".encode()


def rotate(path: Path, keep: int = 3):
    """What spdlog's rotating_file_sink does: .2 -> .3, .1 -> .2, live -> .1, new live file."""
    for i in range(keep - 1, 0, -1):
        src = path.with_name(f"{path.name}.{i}")
        if src.exists():
            os.replace(src, path.with_name(f"{path.name}.{i + 1}"))
    os.replace(path, path.with_name(f"{path.name}.1"))
    path.touch()


def messages(ms: MailsyncProcess) -> list:
    return [l.msg for l in ms.log()]


def test_lines_survive_two_rotations_between_drains(tmp_path):
    ms = tailer(tmp_path)
    log = ms.log_path
    log.write_bytes(line(1) + line(2))
    ms._drain_log()
    assert messages(ms) == ["line 1", "line 2"]
    # Written after the drain, then rotated twice before the next one.
    with open(log, "ab") as f:
        f.write(line(3))
    rotate(log)
    log.write_bytes(line(4))
    rotate(log)
    log.write_bytes(line(5))
    ms._drain_log()
    assert messages(ms) == ["line 1", "line 2", "line 3", "line 4", "line 5"]


def test_one_rotation_still_read(tmp_path):
    ms = tailer(tmp_path)
    log = ms.log_path
    log.write_bytes(line(1))
    ms._drain_log()
    with open(log, "ab") as f:
        f.write(line(2))
    rotate(log)
    log.write_bytes(line(3))
    ms._drain_log()
    assert messages(ms) == ["line 1", "line 2", "line 3"]


def test_partial_last_line_waits(tmp_path):
    ms = tailer(tmp_path)
    log = ms.log_path
    log.write_bytes(line(1) + b"1 [2026-10-28 09:00:00.000] [foreground] [info] half")
    ms._drain_log()
    assert messages(ms) == ["line 1"]
    with open(log, "ab") as f:
        f.write(b" done\n")
    ms._drain_log()
    assert messages(ms) == ["line 1", "half done"]
