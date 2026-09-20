"""pytest entry point: one test per (scenario, server) pair."""
from pathlib import Path

import pytest

from harness.scenario import (ScenarioRun, ScenarioSkipped, ServerSpec, available_server_kinds,
                              discover, load_scenario)

SCENARIOS = Path(__file__).parent / "scenarios"


def _cases(config):
    kinds = available_server_kinds()
    if config.getoption("--servers"):
        kinds = set(config.getoption("--servers").split(","))
    only = config.getoption("--scenario")
    cases = []
    for path in discover(SCENARIOS):
        sc = load_scenario(path)
        if only and only not in sc["name"]:
            continue
        for entry in sc["servers"]:
            spec = ServerSpec.parse(entry)
            marks = [] if spec.kind in kinds else [pytest.mark.skip(reason=f"server kind {spec.kind} not available")]
            cases.append(pytest.param(path, spec, id=f"{sc['name']}[{spec.id}]", marks=marks))
    return cases


def pytest_generate_tests(metafunc):
    if "scenario_path" in metafunc.fixturenames:
        metafunc.parametrize("scenario_path,server_spec", _cases(metafunc.config))


def test_scenario(scenario_path, server_spec, request):
    sc = load_scenario(scenario_path)
    binary = request.config.getoption("--mailsync")
    run = ScenarioRun(sc, server_spec, binary=Path(binary) if binary else None, keep=request.config.getoption("--keep"))
    try:
        failures = run.run()
    except ScenarioSkipped as e:
        pytest.skip(str(e))
    if failures:
        pytest.fail(f"{sc['name']} on {server_spec.id}:\n" + "\n".join(failures)
                    + f"\n\nartifacts: {run.work}\n" + "\n".join(run.report[-15:]), pytrace=False)
    for x in run.xpasses:
        print(f"XPASS {sc['name']} on {server_spec.id}: {x}")
    if run.xfails:
        pytest.xfail("; ".join(run.xfails))
