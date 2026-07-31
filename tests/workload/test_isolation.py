"""Isolation workload tests.

Each task runs a workload that probes an isolation boundary (environment
scrubbing, directory access rules, network namespace) and exits 0 only if the
boundary holds — so the expected status is OK.
"""

import pytest

from conftest import load_config, run_standalone

CONFIG = load_config("isolation.yml")
TASK_IDS = [t["task-id"] for t in CONFIG["tasks"]]


@pytest.fixture(scope="module")
def results():
    return run_standalone(CONFIG)


@pytest.mark.parametrize("task_id", TASK_IDS)
def test_isolation_boundary_holds(results, task_id):
    assert results[task_id] == "OK", (
        f"{task_id} expected OK (boundary held), got {results[task_id]!r}"
    )
