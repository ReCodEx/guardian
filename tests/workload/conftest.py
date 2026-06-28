"""Shared fixtures and helpers for the workload-test tier.

Workload tests drive the real `isolator` binary end-to-end (standalone --yaml
mode today; the three-phase compat lifecycle once B2 lands). They need root and
a cgroup-v2 host, so the whole tier skips cleanly when the binary isn't built or
root isn't available non-interactively.

A *workload* is the in-box payload program a test runs inside the sandbox (built
from tests/workload/workloads/ into tests/workload/build/, mounted as /build).
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest
import yaml

WORKLOAD_DIR = Path(__file__).resolve().parent
REPO_ROOT = WORKLOAD_DIR.parents[1]
ISOLATOR_BIN = REPO_ROOT / "build" / "src" / "isolator"
SCRIPTS = REPO_ROOT / "scripts"
PROBES_DIR = WORKLOAD_DIR / "build"   # compiled workloads, mounted as /build
RES_DIR = WORKLOAD_DIR / "res"        # per-task stats-yaml sink, mounted as /res


def _can_sudo_noninteractive() -> bool:
    if os.geteuid() == 0:
        return True
    try:
        return (
            subprocess.run(["sudo", "-n", "true"], capture_output=True).returncode
            == 0
        )
    except FileNotFoundError:
        return False


@pytest.fixture(scope="session", autouse=True)
def isolator_environment():
    """Set up the shared box/cgroup roots for the whole tier; skip if we can't.

    Skips (rather than fails) when the binary or workloads aren't built or root
    isn't available, so a plain `pytest` on a dev machine is a no-op instead of a
    wall of errors.
    """
    if not ISOLATOR_BIN.exists():
        pytest.skip(
            f"isolator not built at {ISOLATOR_BIN} "
            f"(configure -DTESTING=ON and build first)"
        )
    if not PROBES_DIR.exists() or not any(PROBES_DIR.iterdir()):
        pytest.skip(f"workloads not built into {PROBES_DIR}")
    if not _can_sudo_noninteractive():
        pytest.skip("workload tests need root (passwordless sudo or run as root)")

    subprocess.run([str(SCRIPTS / "isolator_init.sh")], check=False)
    yield
    subprocess.run([str(SCRIPTS / "isolator_cleanup.sh")], check=False)


def load_config(name: str) -> dict:
    """Load a workload yml with ${FILE_DIR} resolved to the workload dir."""
    text = (WORKLOAD_DIR / name).read_text().replace(
        "${FILE_DIR}", str(WORKLOAD_DIR)
    )
    return yaml.safe_load(text)


def run_standalone(config: dict) -> dict[str, str | None]:
    """Run one standalone (--yaml) config and return {task-id: status}.

    The result directory is recreated fresh on each call, so sequential runs
    (e.g. with- vs without-limits) don't read each other's stats files. A task
    whose stats-yaml is missing maps to None.
    """
    if RES_DIR.exists():
        shutil.rmtree(RES_DIR)
    RES_DIR.mkdir(parents=True)

    fd, tmp = tempfile.mkstemp(suffix=".yml")
    with os.fdopen(fd, "w") as f:
        yaml.safe_dump(config, f)
    try:
        subprocess.run(
            ["sudo", str(ISOLATOR_BIN), f"--yaml={tmp}"],
            capture_output=True,
            text=True,
        )
    finally:
        os.unlink(tmp)

    results: dict[str, str | None] = {}
    for task in config.get("tasks", []):
        tid = task["task-id"]
        stats = task.get("stats-yaml", "")
        host = RES_DIR / Path(stats).name  # stats-yaml is box-relative (/res/..)
        results[tid] = (
            yaml.safe_load(host.read_text()).get("status")
            if host.exists()
            else None
        )
    return results
