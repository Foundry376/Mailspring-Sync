import faulthandler
import signal
import sys
from pathlib import Path

import pytest

# `kill -USR1 <pid>` dumps every thread's stack, for when a wait looks stuck. Under xdist the
# pid is the worker's, which names its artifacts directory: test/runs/session-<pid>/.
faulthandler.register(signal.SIGUSR1, all_threads=True)

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def pytest_addoption(parser):
    parser.addoption("--servers", default=None, help="comma-separated server kinds to run (fake,dovecot,cyrus)")
    parser.addoption("--scenario", default=None, help="only scenarios whose name contains this")
    parser.addoption("--mailsync", default=None, help="path to the mailsync binary (else MAILSYNC_BIN / auto)")
    parser.addoption("--keep", action="store_true", help="keep run artifacts for passing scenarios too")


@pytest.hookimpl(tryfirst=True)
def pytest_sessionstart(session):
    """Build missing Docker images here, before xdist starts its workers, so two workers never
    build one at the same time. Images are tagged by a hash of their build context and reused."""
    config = session.config
    if hasattr(config, "workerinput") or config.option.collectonly:
        return
    from harness.scenario import available_server_kinds
    from harness.servers import cyrus, dovecot
    available = available_server_kinds()
    selected = set(config.getoption("--servers").split(",")) if config.getoption("--servers") else available
    # conformance/ compares against Dovecot whatever --servers says.
    if "dovecot" in available and dovecot.default_mode() == "docker":
        dovecot.ensure_image()
    if "cyrus" in available & selected:
        cyrus.ensure_image()


def pytest_terminal_summary(terminalreporter):
    """One list of every scenario that needs a look, with the directory holding its engine log,
    database and server transcript."""
    rows = []
    for status in ("failed", "error", "xfailed", "passed"):
        for rep in terminalreporter.stats.get(status, []):
            props = dict(rep.user_properties)
            if status == "passed" and "xpass" not in props:
                continue
            label = {"failed": "FAILED", "error": "ERROR", "xfailed": "XFAIL", "passed": "XPASS"}[status]
            reason = props.get("reason") or getattr(rep, "wasxfail", "") or props.get("xpass") or str(rep.longrepr or "").split("\n")[-1]
            reason = " ".join(reason.split())[:300]
            rows.append(f"{label} {rep.nodeid}\n    reason: {reason}\n    artifacts: {props.get('artifacts', '-')}")
    if rows:
        terminalreporter.section("scenarios needing attention")
        for row in rows:
            terminalreporter.line(row)
