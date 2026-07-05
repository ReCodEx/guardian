"""Three-phase Isolate-compatibility lifecycle workload test.

Exercises the drop-in path the ReCodEx Worker uses: three independent
invocations keyed by --box-id, sharing state only on disk, emitting an
Isolate-format meta-file and 0/1/2 exit codes (ADR 0001, ADR 0005).

B4 wired --run with a crude exit-code bridge (proxy 0/1, root 0/1/2); the
meta-file and the full exit-code contract land in C1, so only the meta test
stays skipped.
"""

import subprocess
import tempfile
from pathlib import Path

import pytest

from conftest import ISOLATOR_BIN, WORKLOAD_DIR

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


def test_three_phase_run_succeeds():
    try:
        # --init prints the box root on stdout (the Worker appends /box).
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())
        assert box_root.is_dir()

        # --run executes the program inside the box.
        run = _run("--run", "--", "/bin/true")
        assert run.returncode == 0, run.stderr
    finally:
        # --cleanup is idempotent and always reports success.
        assert _run("--cleanup").returncode == 0


def test_run_executes_program_inside_box():
    # Checkpoint B2: the program actually runs and its (inherited) stdout
    # reaches the caller.
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/echo", "hi")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "hi"
    finally:
        _run("--cleanup")


def test_repeated_run_in_same_box():
    # The Worker runs several programs against one --init'd box (compile ->
    # run -> judge); mount points and the box tree persist between runs.
    try:
        assert _run("--init").returncode == 0
        for _ in range(2):
            run = _run("--run", "--", "/bin/echo", "again")
            assert run.returncode == 0, run.stderr
            assert run.stdout.strip() == "again"
    finally:
        _run("--cleanup")


def test_run_before_init_is_box_not_found():
    # A --run on an un-init'd box is an Isolator-internal error (exit 2).
    _run("--cleanup")  # ensure no prior state
    run = _run("--run", "--", "/bin/true")
    assert run.returncode == 2


def test_nonzero_program_exit_is_code_one():
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/false")
        assert run.returncode == 1  # program ran but result != OK
    finally:
        _run("--cleanup")


@pytest.mark.skip(reason="meta-file lands in C1 (meta pipe + writer)")
def test_meta_file_written(tmp_path):
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/true", meta=str(meta))
        assert run.returncode == 0, run.stderr
        assert _parse_meta(meta).get("status", "OK") == "OK"
    finally:
        _run("--cleanup")
