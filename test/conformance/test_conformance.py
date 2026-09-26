"""The fake server must answer like Dovecot for every exchange mailsync performs.
Needs Docker (or a local dovecot); skipped otherwise."""
import pytest

from harness.scenario import available_server_kinds
from conformance.compare import compare_all
from conformance.probes import PROBES


@pytest.mark.skipif("dovecot" not in available_server_kinds(), reason="no Dovecot available")
@pytest.mark.parametrize("probe", PROBES, ids=lambda p: p.__name__)
def test_fake_matches_dovecot(probe):
    results = compare_all(probes=[probe])
    bad = {k: v for k, (status, v) in results.items() if status == "DIFFERENT"}
    if bad:
        msg = "\n\n".join(f"{k[0]}/{k[1]}:\n" + "\n".join(diff[2:]) for k, diff in bad.items())
        pytest.fail(f"fake differs from Dovecot in {len(bad)} response(s):\n{msg}", pytrace=False)
