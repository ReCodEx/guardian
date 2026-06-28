"""Three-phase Isolate-compatibility lifecycle workload test.

Exercises the drop-in path the ReCodEx Worker uses: three independent
invocations keyed by --box-id, sharing state only on disk, emitting an
Isolate-format meta-file and 0/1/2 exit codes (ADR 0001, ADR 0005).

SKIPPED for now: B2 mode dispatch is not wired yet — root_core::run() still
runs the monolithic single-shot flow regardless of --init/--run/--cleanup
(it ignores cli::run_mode). This module is the executable spec for that work;
drop the skip when B2 lands.
"""

import subprocess
import tempfile
from pathlib import Path

import pytest

from conftest import ISOLATOR_BIN, WORKLOAD_DIR

pytestmark = pytest.mark.skip(
    reason="B2 mode dispatch not wired: root_core::run() ignores run_mode"
)

BOX_ID = 0


def _run(*args, meta=None):
    cmd = ["sudo", str(ISOLATOR_BIN), f"--box-id={BOX_ID}"]
    if meta:
        cmd.append(f"--meta={meta}")
    cmd.extend(args)
    return subprocess.run(cmd, capture_output=True, text=True)


def _parse_meta(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text().splitlines():
        if ":" in line:
            k, _, v = line.partition(":")
            out[k.strip()] = v.strip()
    return out


def test_three_phase_run_succeeds(tmp_path):
    meta = tmp_path / "meta.txt"
    try:
        # --init prints the box root on stdout (the Worker appends /box).
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())
        assert box_root.is_dir()

        # --run executes the program and writes the Isolate meta-file.
        run = _run("--run", "--", "/bin/true", meta=str(meta))
        assert run.returncode == 0, run.stderr
        assert _parse_meta(meta).get("status", "OK") == "OK"
    finally:
        # --cleanup is idempotent and always reports success.
        assert _run("--cleanup").returncode == 0


def test_run_before_init_is_box_not_found(tmp_path):
    # A --run on an un-init'd box is an Isolator-internal error (exit 2).
    meta = tmp_path / "meta.txt"
    _run("--cleanup")  # ensure no prior state
    run = _run("--run", "--", "/bin/true", meta=str(meta))
    assert run.returncode == 2


def test_nonzero_program_exit_is_code_one(tmp_path):
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/false", meta=str(meta))
        assert run.returncode == 1  # program ran but result != OK
    finally:
        _run("--cleanup")
