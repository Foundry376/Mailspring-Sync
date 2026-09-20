#!/usr/bin/env python3
"""
Before/after regression comparison: run the scenario catalog against one mailsync binary,
save the outcomes, and diff two such runs.

    # record a baseline (once), then the candidate
    python3 test/tools/ab.py run --mailsync /path/to/mailsync-before --out test/ab/before.json
    python3 test/tools/ab.py run --mailsync ../app/mailsync           --out test/ab/after.json
    # compare
    python3 test/tools/ab.py compare test/ab/before.json test/ab/after.json

Outcomes per (scenario, server): PASS, FAIL, XFAIL (a known bug failed as expected), XPASS
(a known bug no longer reproduces), SKIP, ERROR. The comparison lists every pair whose
outcome changed and exits non-zero if any change is a regression (PASS->FAIL/ERROR,
XPASS->FAIL, or a new failure in a scenario that passed before).
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from harness.scenario import (ScenarioRun, ScenarioSkipped, ServerSpec, available_server_kinds,  # noqa: E402
                              discover, load_scenario)

REGRESSION = {("PASS", "FAIL"), ("PASS", "ERROR"), ("XPASS", "FAIL"), ("XPASS", "XFAIL"),
              ("XFAIL", "ERROR"), ("PASS", "XFAIL")}


def run_all(binary, servers, only, keep):
    kinds = set(servers.split(",")) if servers else available_server_kinds()
    results = {}
    for path in discover(HERE / "scenarios"):
        sc = load_scenario(path)
        if only and only not in sc["name"]:
            continue
        for entry in sc["servers"]:
            spec = ServerSpec.parse(entry)
            key = f"{sc['name']}[{spec.id}]"
            if spec.kind not in kinds:
                results[key] = {"outcome": "SKIP", "detail": f"server kind {spec.kind} unavailable"}
                continue
            t0 = time.time()
            run = ScenarioRun(sc, spec, binary=Path(binary) if binary else None, keep=keep)
            try:
                failures = run.run()
            except ScenarioSkipped as e:
                results[key] = {"outcome": "SKIP", "detail": str(e)}
                print(f"SKIP  {key}", flush=True)
                continue
            except Exception as e:  # harness error, not an engine verdict
                results[key] = {"outcome": "ERROR", "detail": f"{type(e).__name__}: {e}"}
                print(f"ERROR {key}: {e}", flush=True)
                continue
            if failures:
                outcome, detail = "FAIL", "\n".join(failures)[:2000]
            elif run.xfails:
                # XPASS only when every xfail'd expectation passed; a partial flip is still XFAIL
                outcome, detail = "XFAIL", "; ".join(run.xfails) + ("; XPASS: " + "; ".join(run.xpasses) if run.xpasses else "")
            elif run.xpasses:
                outcome, detail = "XPASS", "; ".join(run.xpasses)
            else:
                outcome, detail = "PASS", ""
            results[key] = {"outcome": outcome, "detail": detail, "seconds": round(time.time() - t0, 1),
                            "xfails": run.xfails, "xpasses": run.xpasses}
            print(f"{outcome:5} {key} ({results[key]['seconds']}s)", flush=True)
    return results


def cmd_run(args):
    results = run_all(args.mailsync, args.servers, args.scenario, args.keep)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"binary": args.mailsync, "recorded": time.strftime("%Y-%m-%d %H:%M:%S"),
                               "results": results}, indent=2) + "\n")
    tally = {}
    for r in results.values():
        tally[r["outcome"]] = tally.get(r["outcome"], 0) + 1
    print(f"\nwrote {out}: " + ", ".join(f"{k} {v}" for k, v in sorted(tally.items())))
    return 0


def cmd_compare(args):
    before = json.loads(Path(args.before).read_text())
    after = json.loads(Path(args.after).read_text())
    b, a = before["results"], after["results"]
    print(f"before: {before['binary']} ({before['recorded']})\nafter:  {after['binary']} ({after['recorded']})\n")
    changed, regressions = [], []
    for key in sorted(set(b) | set(a)):
        ob = b.get(key, {}).get("outcome", "-")
        oa = a.get(key, {}).get("outcome", "-")
        if ob != oa:
            changed.append((key, ob, oa))
            if (ob, oa) in REGRESSION or (oa in ("FAIL", "ERROR") and ob not in ("FAIL", "ERROR")):
                regressions.append((key, ob, oa))
        elif ob == oa == "FAIL" and b[key].get("detail") != a[key].get("detail"):
            changed.append((key, "FAIL", "FAIL (different failure)"))
    same = len(set(b) & set(a)) - len(changed)
    print(f"{same} unchanged, {len(changed)} changed, {len(regressions)} regressions\n")
    for key, ob, oa in changed:
        flag = "REGRESSION" if (key, ob, oa) in regressions else ("fixed" if oa in ("PASS", "XPASS") else "changed")
        print(f"{flag:10} {key}: {ob} -> {oa}")
        if args.verbose or flag == "REGRESSION":
            for label, res in (("before", b.get(key)), ("after", a.get(key))):
                if res and res.get("detail"):
                    print(f"    {label}: " + res["detail"].replace("\n", "\n            ")[:800])
    if not changed:
        print("no outcome changed")
    return 1 if regressions else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--mailsync", help="binary to test (default: auto-discovered)")
    r.add_argument("--out", required=True)
    r.add_argument("--servers", help="fake,dovecot (default: whatever is available)")
    r.add_argument("--scenario", help="only scenarios whose name contains this")
    r.add_argument("--keep", action="store_true")
    r.set_defaults(fn=cmd_run)
    c = sub.add_parser("compare")
    c.add_argument("before")
    c.add_argument("after")
    c.add_argument("-v", "--verbose", action="store_true")
    c.set_defaults(fn=cmd_compare)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
