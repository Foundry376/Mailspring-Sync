import faulthandler
import signal
import sys
from pathlib import Path

# `kill -USR1 <pytest pid>` dumps every thread's stack, for when a wait looks stuck.
faulthandler.register(signal.SIGUSR1, all_threads=True)

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def pytest_addoption(parser):
    parser.addoption("--servers", default=None, help="comma-separated server kinds to run (fake,dovecot)")
    parser.addoption("--scenario", default=None, help="only scenarios whose name contains this")
    parser.addoption("--mailsync", default=None, help="path to the mailsync binary (else MAILSYNC_BIN / auto)")
    parser.addoption("--keep", action="store_true", help="keep run artifacts for passing scenarios too")
