"""Resource-limit workload tests.

Each task in limits.yml runs a workload that deliberately exceeds one limit.
The contract is two-sided: with the limit applied the task must be terminated
(status != OK), and with the limit removed the same workload must complete (OK)
— proving the failure is caused by the limit, not the workload itself.
"""

import copy

import pytest

from conftest import load_config, run_standalone

CONFIG = load_config("limits.yml")
TASK_IDS = [t["task-id"] for t in CONFIG["tasks"]]


@pytest.fixture(scope="module")
def with_limits():
    return run_standalone(CONFIG)


@pytest.fixture(scope="module")
def without_limits():
    cfg = copy.deepcopy(CONFIG)
    for task in cfg["tasks"]:
        task.pop("limits", None)
    return run_standalone(cfg)


@pytest.mark.parametrize("task_id", TASK_IDS)
def test_workload_succeeds_without_limit(without_limits, task_id):
    assert without_limits[task_id] == "OK", (
        f"{task_id} should succeed once its limit is removed, "
        f"got {without_limits[task_id]!r}"
    )


@pytest.mark.parametrize("task_id", TASK_IDS)
def test_limit_is_enforced(with_limits, task_id):
    status = with_limits[task_id]
    assert status not in (None, "OK"), (
        f"{task_id} should be terminated by its limit, got {status!r}"
    )
