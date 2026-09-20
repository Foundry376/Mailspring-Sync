#!/usr/bin/env python3
"""Run one scenario from the command line and print the report.

    python3 test/run.py test/scenarios/baseline-initial-sync.yaml [--server fake:dovecot] [--keep]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness.scenario import ScenarioRun, ScenarioSkipped, ServerSpec, load_scenario  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario")
    ap.add_argument("--server", help="fake:<personality> or dovecot:<profile>; default: the scenario's first")
    ap.add_argument("--mailsync", help="path to mailsync binary")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    sc = load_scenario(Path(args.scenario))
    spec = ServerSpec.parse(args.server) if args.server else ServerSpec.parse(sc["servers"][0])
    run = ScenarioRun(sc, spec, binary=Path(args.mailsync) if args.mailsync else None, keep=True if args.keep else False)
    try:
        failures = run.run()
    except ScenarioSkipped as e:
        print("SKIPPED:", e)
        return 0
    print("\n".join(run.report))
    if failures:
        print(f"\nFAILED ({len(failures)}):")
        for f in failures:
            print(" -", f)
        print(f"artifacts: {run.work}")
        return 1
    print("\nPASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
