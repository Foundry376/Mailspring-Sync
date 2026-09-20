"""Run every probe against a freshly populated fake and Dovecot, and report the differences."""
import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness import mailgen  # noqa: E402
from harness.servers.dovecot import DovecotServer  # noqa: E402
from harness.servers.fake import FakeServer  # noqa: E402
from conformance.probes import PROBES, Rig  # noqa: E402

# Differences that are legitimate implementation freedom, keyed by (probe, label).
# Each entry says why the fake is allowed to differ. Anything not listed is a failure.
ALLOWED_DIFFERENCES = {
}


def populate(server):
    server.populate("INBOX", mailgen.messages(3) + [mailgen.message(4, html=True, attachment=("a.txt", b"hello"))]
                    + mailgen.messages(8, start=5))
    server.set_flags("INBOX", [2, 3], remove=["\\Seen"])
    server.populate("Archive", mailgen.messages(2, start=100))


def run_probe(probe, make_server):
    server = make_server()
    server.start()
    try:
        populate(server)
        rig = Rig(server)
        try:
            return probe(rig)
        finally:
            rig.close()
    finally:
        server.stop()


def compare_all(fake_personality="dovecot", dovecot_profile="qresync", probes=None, verbose=False):
    results = {}
    for probe in probes or PROBES:
        fake = run_probe(probe, lambda: FakeServer(fake_personality))
        real = run_probe(probe, lambda: DovecotServer(dovecot_profile))
        for label in sorted(set(fake) | set(real)):
            f = [l.decode("utf-8", "replace") for l in fake.get(label, [])]
            r = [l.decode("utf-8", "replace") for l in real.get(label, [])]
            key = (probe.__name__, label)
            if f == r:
                results[key] = ("same", [])
            else:
                diff = list(difflib.unified_diff(r, f, "dovecot", "fake", lineterm="", n=1))
                results[key] = ("allowed" if key in ALLOWED_DIFFERENCES else "DIFFERENT", diff)
    return results


def main():
    verbose = "-v" in sys.argv
    only = [a for a in sys.argv[1:] if not a.startswith("-")]
    probes = [p for p in PROBES if not only or any(o in p.__name__ for o in only)]
    results = compare_all(probes=probes)
    bad = 0
    for (probe, label), (status, diff) in results.items():
        if status == "same" and not verbose:
            continue
        print(f"== {probe}/{label}: {status}")
        if status != "same":
            print("   " + "\n   ".join(diff[2:]))
        if status == "allowed":
            print("   allowed:", ALLOWED_DIFFERENCES[(probe, label)])
        if status == "DIFFERENT":
            bad += 1
    print(f"\n{len(results)} comparisons, {bad} unexplained differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
