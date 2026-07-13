"""Three-phase Isolate-compatibility lifecycle workload test.

Exercises the drop-in path the ReCodEx Worker uses: three independent
invocations keyed by --box-id, sharing state only on disk, emitting an
Isolate-format meta-file and 0/1/2 exit codes (ADR 0001, ADR 0005).

B4 wired --run with a crude exit-code bridge (proxy 0/1, root 0/1/2); the
meta-file and the full exit-code contract land in C1, so only the meta test
stays skipped.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from conftest import ISOLATOR_BIN, PROBES_DIR, WORKLOAD_DIR

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


def test_meta_file_ok_omits_status(tmp_path):
    # An OK run writes exitcode:0 and — matching Isolate — no status line
    # at all (ADR 0005 C1).
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/true", meta=str(meta))
        assert run.returncode == 0, run.stderr
        parsed = _parse_meta(meta)
        assert "status" not in parsed
        assert parsed.get("exitcode") == "0"
    finally:
        _run("--cleanup")


def test_meta_file_nonzero_exit_is_re(tmp_path):
    # A program that exits non-zero: status:RE, exitcode:N, Isolator exit 1.
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/false", meta=str(meta))
        assert run.returncode == 1, run.stderr
        parsed = _parse_meta(meta)
        assert parsed.get("status") == "RE"
        assert parsed.get("exitcode") == "1"
    finally:
        _run("--cleanup")


def test_meta_file_wall_time_exceeded_is_to_killed(tmp_path):
    # A program that overruns --wall-time is SIGKILLed by the Isolator:
    # status:TO plus the killed:1 discriminator (slice 3). /bin/sleep is in the
    # default box (like /bin/echo above), so no dir-rule (#11) is needed.
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--wall-time=1", "--run", "--", "/bin/sleep", "5",
                   meta=str(meta))
        assert run.returncode == 1, run.stderr
        parsed = _parse_meta(meta)
        assert parsed.get("status") == "TO"
        assert parsed.get("killed") == "1"
    finally:
        _run("--cleanup")


def test_default_tmp_is_writable():
    # #11: /tmp is a default mount (isolate's tmp:tmp). A task can write and
    # read it back. /bin/sh is in the default box, so no dir-rule is needed.
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/sh", "-c",
                   "echo hi > /tmp/probe && cat /tmp/probe")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "hi"
    finally:
        _run("--cleanup")


def test_default_tmp_is_isolated_from_host():
    # #11: the box /tmp is a fresh box-owned scratch, NOT a bind of the host
    # /tmp — a sentinel placed in the host /tmp must be invisible inside the box.
    sentinel = Path(tempfile.gettempdir()) / f"rcdx_host_sentinel_{os.getpid()}"
    sentinel.write_text("host-only")
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/sh", "-c",
                   f"test -e /tmp/{sentinel.name} && echo LEAK || echo ISOLATED")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "ISOLATED"
    finally:
        _run("--cleanup")
        sentinel.unlink(missing_ok=True)


def test_default_dev_shm_is_writable():
    # #11: /dev/shm is a default tmpfs mount (isolate's dev/shm=tmpfs:fs:rw),
    # writable by the task.
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", "--", "/bin/sh", "-c",
                   "echo hi > /dev/shm/probe && cat /dev/shm/probe")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "hi"
    finally:
        _run("--cleanup")


def test_default_tmp_strips_setuid():
    # #11: /tmp is bind-mounted MS_NOSUID, so a setuid-root binary there does
    # NOT elevate. The box user can't create such a file itself, so we plant a
    # root-owned 4755 probe into the box's /tmp scratch (box_root/tmp is the
    # bind-self source) as root, then exec it inside the box: under nosuid its
    # effective UID stays the box user (box_id + 60000), not 0.
    probe = PROBES_DIR / "euid_probe"
    if not probe.exists():
        pytest.skip("euid_probe workload not built")
    box_uid = BOX_ID + 60000
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())

        box_tmp = box_root / "tmp"
        box_tmp.mkdir(parents=True, exist_ok=True)
        planted = box_tmp / "euid_probe"
        shutil.copy(probe, planted)
        os.chown(planted, 0, 0)  # root-owned
        os.chmod(planted, 0o4755)  # setuid bit set

        run = _run("--run", "--", "/tmp/euid_probe")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == str(box_uid), (
            f"expected euid {box_uid} (nosuid stripped setuid); "
            f"got {run.stdout.strip()!r}"
        )
    finally:
        _run("--cleanup")


def test_meta_file_internal_error_is_xx(tmp_path):
    # A --run on an un-init'd box is an Isolator-internal error: exit 2 and a
    # status:XX meta with a message, written by root's terminate() meta-sink
    # (ADR 0005 C2).
    meta = tmp_path / "meta.txt"
    _run("--cleanup")  # ensure no prior state
    run = _run("--run", "--", "/bin/true", meta=str(meta))
    assert run.returncode == 2
    parsed = _parse_meta(meta)
    assert parsed.get("status") == "XX"
    assert "message" in parsed
