"""Three-phase Isolate-compatibility lifecycle workload test.

Exercises the drop-in path the ReCodEx Worker uses: three independent
invocations keyed by --box-id, sharing state only on disk, emitting an
Isolate-format meta-file and 0/1/2 exit codes.

B4 wired --run with a crude exit-code bridge (proxy 0/1, root 0/1/2); the
meta-file and the full exit-code contract land in C1, so only the meta test
stays skipped.
"""

import os
import shutil
import stat
import subprocess
import tempfile
import time
from pathlib import Path

import pytest

from conftest import GUARDIAN_BIN, PROBES_DIR, WORKLOAD_DIR

BOX_ID = 0


@pytest.fixture
def setuid_run():
    """Invoke a root-owned 4755 copy of the Guardian as the non-root caller.

    Emulates the real install. Yields (run, caller_uid). Skips if we cannot drop to a
    non-root uid.
    """
    sudo_uid = os.environ.get("SUDO_UID")
    if os.geteuid() != 0 or not sudo_uid:
        pytest.skip("need root + a non-root SUDO_UID to emulate a setuid install")
    caller = int(sudo_uid)

    suid_bin = GUARDIAN_BIN.parent / "guardian_suid"
    shutil.copy(GUARDIAN_BIN, suid_bin)
    os.chown(suid_bin, 0, 0)
    os.chmod(suid_bin, 0o4755)

    def run(*args):
        return subprocess.run(
            [str(suid_bin), f"--box-id={BOX_ID}", *args],
            capture_output=True,
            text=True,
            preexec_fn=lambda: os.setuid(caller),  # noqa: E731
        )

    try:
        yield run, caller
    finally:
        _run("--cleanup")  # sudo backstop
        suid_bin.unlink(missing_ok=True)


def _run(*args, meta=None, env_vars=None):
    cmd = ["sudo"]
    # `sudo KEY=VAL cmd` sets KEY in the launched recodex-guardian's environment, so
    # --env=KEY inherit rules have something to inherit (sudo scrubs env).
    if env_vars:
        cmd += [f"{k}={v}" for k, v in env_vars.items()]
    cmd += [str(GUARDIAN_BIN), f"--box-id={BOX_ID}"]
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


def test_non_root_invocation_is_rejected():
    # The startup privilege gate (credentials::require_root) fails fast when the
    # effective uid is not 0, instead of crashing deep in clone3()/mount(). The
    # suite runs under sudo, so drop the child to the invoking (non-root) user
    # via SUDO_UID; when the suite is already non-root, run it directly. Either
    # way the Guardian sees euid != 0. No box state is created — it dies at the
    # gate, so no --cleanup is needed.
    preexec = None
    if os.geteuid() == 0:
        sudo_uid = os.environ.get("SUDO_UID")
        if not sudo_uid:
            pytest.skip("no non-root uid available to exercise the gate")
        drop_to = int(sudo_uid)
        preexec = lambda: os.setuid(drop_to)  # noqa: E731

    proc = subprocess.run(
        [str(GUARDIAN_BIN), f"--box-id={BOX_ID}", "--init"],
        capture_output=True,
        text=True,
        preexec_fn=preexec,
    )
    assert proc.returncode == 2, proc.stderr
    assert "Must be started as root" in proc.stderr


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


def test_run_lazily_creates_cgroup_parent_and_uses_new_paths():
    """Slice A / ADR 0007: --run self-arranges the shared recodex-guardian cgroup
    parent with no boot/init script, and the box tree lives under the relocated
    /var/lib/recodex-guardian/boxes.

    The shared parent is torn down first so a success proves lazy *creation*
    (dir + controllers), not a leftover from an earlier test or session.
    """
    cg_parent = Path("/sys/fs/cgroup/recodex-guardian")

    # Clean slate: drop any box state, then depth-first rmdir the shared cgroup
    # parent so we exercise lazy creation rather than reuse.
    _run("--cleanup")
    subprocess.run(
        ["sudo", "find", str(cg_parent), "-type", "d", "-depth",
         "-exec", "rmdir", "{}", ";"],
        capture_output=True,
    )
    assert not cg_parent.exists(), "precondition: shared cgroup parent removed"

    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())
        # Box tree relocated from /isolate_boxes to /var/lib/recodex-guardian/boxes.
        assert box_root == Path(f"/var/lib/recodex-guardian/boxes/{BOX_ID}"), box_root
        assert box_root.is_dir()

        # --run with no prior init script must succeed — which is only possible
        # if --run lazily created the cgroup parent and enabled its controllers.
        run = _run("--run", "--", "/bin/true")
        assert run.returncode == 0, run.stderr

        # The parent exists and carries cpu/memory/pids in its subtree_control
        # (proof the idempotent enable ran, not merely the mkdir). cpuset is
        # enabled on every --run, pinned or not.
        assert cg_parent.is_dir()
        enabled = set(
            (cg_parent / "cgroup.subtree_control").read_text().split()
        )
        assert {"cpu", "cpuset", "memory", "pids"} <= enabled, enabled
    finally:
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
    # A --run on an un-init'd box is an Guardian-internal error (exit 2).
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
    # A program that exits non-zero: status:RE, exitcode:N, Guardian exit 1.
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
    # A program that overruns --wall-time is SIGKILLed by the Guardian:
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


def test_setuid_invocation_runs_task_as_box_uid(setuid_run):
    # #13 acceptance: the binary installed setuid-root and invoked by a NON-root
    # user must complete init/run/cleanup, and the task must run as box_uid —
    # not root, not the invoking caller.
    probe = PROBES_DIR / "euid_probe"
    if not probe.exists():
        pytest.skip("euid_probe workload not built")
    run, _caller = setuid_run
    box_uid = BOX_ID + 60000

    init = run("--init")
    assert init.returncode == 0, init.stderr
    box_root = Path(init.stdout.strip())
    assert box_root.is_dir()

    # Plant a plain (non-setuid) probe the box user can exec.
    box_tmp = box_root / "tmp"
    box_tmp.mkdir(parents=True, exist_ok=True)
    planted = box_tmp / "euid_probe"
    shutil.copy(probe, planted)
    os.chmod(planted, 0o755)

    r = run("--run", "--", "/tmp/euid_probe")
    assert r.returncode == 0, r.stderr
    assert r.stdout.strip() == str(box_uid), (
        f"task must run as box_uid {box_uid} under a setuid invocation, "
        f"not root or the caller; got {r.stdout.strip()!r}"
    )

    assert run("--cleanup").returncode == 0


def test_init_box_dir_is_private_0700():
    # #13 Slice C: --init creates box/ mode 0700 owned by the caller (orig_uid),
    # retiring the world-writable 0777 status quo. The caller owns it outright,
    # so no world bits are needed to stage files. Under the sudo runner the
    # caller is root.
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box = Path(init.stdout.strip()) / "box"
        st = box.stat()
        assert stat.S_IMODE(st.st_mode) == 0o700, oct(st.st_mode)
        assert st.st_uid == 0  # orig_uid == root under sudo
    finally:
        _run("--cleanup")


def test_setuid_ownership_dance(setuid_run):
    # #13 Slice C, end to end under a genuine setuid invocation (caller =
    # SUDO_UID, distinct from box_uid): --init hands box/ to the caller at 0700;
    # the caller stages an input; --run flips box/ to box_uid so the task reads
    # the input and writes an output; the run-end flip hands box/ back so the
    # caller owns the output and can collect it.
    run, caller = setuid_run

    init = run("--init")
    assert init.returncode == 0, init.stderr
    box = Path(init.stdout.strip()) / "box"

    st = box.stat()
    assert stat.S_IMODE(st.st_mode) == 0o700, oct(st.st_mode)
    assert st.st_uid == caller, "init must hand box/ to the caller"

    # Stage an input owned by the caller (the Worker's job-file staging step).
    inp = box / "in.txt"
    inp.write_text("dance")
    os.chown(inp, caller, caller)

    # The task copies the staged input to an output. Success proves it could
    # read the input (run-start flip to box_uid) and write into box/.
    r = run("--run", "--", "/bin/sh", "-c", "cat /box/in.txt > /box/out.txt")
    assert r.returncode == 0, r.stderr

    out = box / "out.txt"
    assert out.read_text() == "dance"
    # run-end flip: the task-produced output is owned by the caller again.
    assert out.stat().st_uid == caller, "run-end must hand box/ back to caller"


def test_setuid_meta_file_is_caller_owned(setuid_run):
    # #13 Slice D: the caller-supplied --meta file is opened under the caller's
    # filesystem identity (Isolate's switch_fsid_to_caller), so root's DAC
    # override cannot be tricked into creating or clobbering a file the caller
    # could not reach itself. The observable proof is ownership: the meta file
    # comes out owned by the caller (orig_uid), not root.
    #
    # Because the fsid'd open is permission-checked as the caller, EVERY
    # component of the path — not just the leaf dir — must be caller-traversable.
    # pytest's tmp_path lives under a root-owned 0700 /tmp/pytest-of-root, which
    # the caller cannot enter; so use a dir the caller owns directly under
    # world-traversable /tmp (1777).
    run, caller = setuid_run

    metadir = Path(tempfile.mkdtemp(prefix="guardian_meta_"))
    os.chown(metadir, caller, caller)
    meta = metadir / "meta.txt"
    try:
        assert run("--init").returncode == 0
        r = run("--run", f"--meta={meta}", "--", "/bin/true")
        assert r.returncode == 0, r.stderr

        assert meta.exists(), "meta file was not written"
        assert meta.stat().st_uid == caller, (
            "meta file must be created under the caller's fs identity (the fsid "
            f"dance); got uid {meta.stat().st_uid}"
        )
        # Sanity: a real meta file, not an empty artefact left by a failed open.
        assert "time" in _parse_meta(meta)
    finally:
        shutil.rmtree(metadir, ignore_errors=True)


def test_compat_dir_binds_host_input(tmp_path):
    # #11: a --dir rule makes a host directory readable inside the box (the
    # "read a bound input" acceptance criterion). The box user (60000) reads
    # through the bind mount, so the payload dir/file must be accessible to it.
    payload = tmp_path / "payload"
    payload.mkdir()
    (payload / "data.txt").write_text("bound-input-42")
    os.chmod(payload, 0o755)
    os.chmod(payload / "data.txt", 0o644)
    try:
        assert _run("--init").returncode == 0
        run = _run(f"--dir=data={payload}", "--run", "--",
                   "/bin/cat", "/data/data.txt")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "bound-input-42"
    finally:
        _run("--cleanup")


def test_setuid_bind_mount_denied_for_caller_inaccessible_dir(setuid_run):
    # #13 Slice E: a --dir bind is mounted with the caller's uid/gid + only
    # CAP_SYS_ADMIN (no DAC_OVERRIDE), so it cannot bind a host path the caller
    # could not reach itself — Isolate's drop-priv mount ("if the mounted path
    # contains elements inaccessible to the user"). The unreachable element must
    # be an *ancestor*: binding the leaf dir itself needs no permission on it,
    # only the ability to resolve the path to it. So we bind <outer>/payload
    # where <outer> is root-owned 0700 — the caller cannot traverse it, so the
    # bind's path resolution (as the caller) is refused and --run dies (exit 2).
    # Run as full root (the status quo), DAC-override would walk straight in.
    run, _caller = setuid_run

    outer = Path(tempfile.mkdtemp(prefix="guardian_secret_"))
    os.chown(outer, 0, 0)
    os.chmod(outer, 0o700)  # caller cannot traverse this ancestor
    inner = outer / "payload"
    inner.mkdir()
    os.chmod(inner, 0o755)
    (inner / "data.txt").write_text("top-secret")
    os.chmod(inner / "data.txt", 0o644)
    try:
        assert run("--init").returncode == 0
        r = run(f"--dir=secret={inner}", "--run", "--",
                "/bin/cat", "/secret/data.txt")
        assert r.returncode == 2, (
            "bind through a caller-inaccessible ancestor must be refused "
            f"(drop-priv mount), got rc={r.returncode}: {r.stdout!r} {r.stderr!r}"
        )
        assert "Mount failed" in r.stderr, r.stderr
    finally:
        shutil.rmtree(outer, ignore_errors=True)


def test_setuid_bind_mount_allowed_for_caller_accessible_dir(setuid_run):
    # #13 Slice E, the positive companion: a bind of a host dir the caller CAN
    # reach still works under the drop — the drop only removes DAC-override, it
    # does not block legitimate binds. The dir is caller-owned and world-readable
    # (the box user reads through the bind at run time); every path component is
    # caller-traversable so the mount-time check as the caller passes.
    run, caller = setuid_run

    payload = Path(tempfile.mkdtemp(prefix="guardian_payload_"))
    os.chown(payload, caller, caller)
    os.chmod(payload, 0o755)
    data = payload / "data.txt"
    data.write_text("bound-ok-7")
    os.chown(data, caller, caller)
    os.chmod(data, 0o644)
    try:
        assert run("--init").returncode == 0
        r = run(f"--dir=data={payload}", "--run", "--",
                "/bin/cat", "/data/data.txt")
        assert r.returncode == 0, r.stderr
        assert r.stdout.strip() == "bound-ok-7"
    finally:
        shutil.rmtree(payload, ignore_errors=True)


def test_compat_dir_tmp_override_keeps_tmp_writable():
    # #11: the Worker sends --dir=/tmp:tmp, whose inner path collides with the
    # default tmp:tmp. The override must replace the default (not double-mount),
    # leaving /tmp writable.
    try:
        assert _run("--init").returncode == 0
        run = _run("--dir=/tmp:tmp", "--run", "--", "/bin/sh", "-c",
                   "echo hi > /tmp/probe && cat /tmp/probe")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "hi"
    finally:
        _run("--cleanup")


# --- --env / IO redirection / --chdir / --share-net compat verification ------


def test_compat_env_set():
    # #11: --env=K=V sets a variable in the task environment.
    try:
        assert _run("--init").returncode == 0
        run = _run("--env=FOO=bar", "--run", "--", "/bin/sh", "-c", "echo $FOO")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "bar"
    finally:
        _run("--cleanup")


def test_compat_env_inherit():
    # #11: bare --env=K inherits K from the Guardian's own environment.
    try:
        assert _run("--init").returncode == 0
        run = _run("--env=FOO", "--run", "--", "/bin/sh", "-c", "echo $FOO",
                   env_vars={"FOO": "inherited"})
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "inherited"
    finally:
        _run("--cleanup")


def test_compat_stdout_redirect_collectable_by_caller():
    # --stdout captures the program's output to a file inside box/. The task
    # creates it as box_uid during the run (#11 privilege-order fix), then the
    # Slice C run-end flip hands it back to the caller so the Worker can collect
    # it. Under the sudo runner the caller (orig_uid) is root, so it ends up
    # root-owned — by the deliberate chown-back, not the old privilege bug; a
    # caller==non-root check is in test_setuid_ownership_dance.
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())
        run = _run("--stdout=/box/out.txt", "--run", "--", "/bin/echo", "captured")
        assert run.returncode == 0, run.stderr
        out = box_root / "box" / "out.txt"
        assert out.read_text().strip() == "captured"
        assert out.stat().st_uid == 0, "run-end flip must hand output to caller"
    finally:
        _run("--cleanup")


def test_compat_stdin_redirect():
    # #11: --stdin feeds a file to the program's stdin. The file is opened as
    # the box user, so it lives in the box-writable /box.
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        box_root = Path(init.stdout.strip())
        src = box_root / "box" / "in.txt"
        src.write_text("fed-via-stdin")
        os.chmod(src, 0o644)
        run = _run("--stdin=/box/in.txt", "--run", "--", "/bin/cat")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "fed-via-stdin"
    finally:
        _run("--cleanup")


def test_compat_stderr_to_stdout():
    # #11: --stderr-to-stdout merges the task's stderr into its stdout.
    try:
        assert _run("--init").returncode == 0
        run = _run("--stderr-to-stdout", "--run", "--", "/bin/sh", "-c",
                   "echo oops >&2")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "oops"
    finally:
        _run("--cleanup")


def test_compat_chdir():
    # #11: --chdir sets the task's working directory (relative to the box root).
    try:
        assert _run("--init").returncode == 0
        run = _run("--chdir=/tmp", "--run", "--", "/bin/sh", "-c", "pwd")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "/tmp"
    finally:
        _run("--cleanup")


def _iface_count():
    # Count network interfaces visible inside the box via /proc/net/dev.
    run = _run("--run", "--", "/bin/sh", "-c", 'grep -c ":" /proc/net/dev')
    assert run.returncode == 0, run.stderr
    return int(run.stdout.strip())


def _iface_count_shared():
    run = _run("--share-net", "--run", "--", "/bin/sh", "-c",
               'grep -c ":" /proc/net/dev')
    assert run.returncode == 0, run.stderr
    return int(run.stdout.strip())


def test_compat_share_net_toggles_network_namespace():
    # #11: default is an isolated net namespace (loopback only); --share-net
    # joins the host's, exposing its interfaces.
    try:
        assert _run("--init").returncode == 0
        assert _iface_count() == 1  # lo only
        assert _iface_count_shared() > 1  # host interfaces visible
    finally:
        _run("--cleanup")


def test_compat_bare_processes_means_unlimited():
    # #21: bare --processes is Isolate's *unlimited* sentinel, and the Worker
    # emits it on every sandboxed task by default. A pipeline needs to fork, so
    # it only runs if no cap was applied.
    try:
        assert _run("--init").returncode == 0
        run = _run("--processes", "--run", "--", "/bin/sh", "-c",
                   "echo forked | /bin/cat")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "forked"
    finally:
        _run("--cleanup")


def test_compat_processes_cap_still_bites():
    # Companion: treating 0 as "no cap" must not disable real caps. The same
    # pipeline succeeds with room to fork and fails when only the shell fits, so
    # a pass can't come from the limit being ignored.
    try:
        assert _run("--init").returncode == 0

        ok = _run("--processes=8", "--run", "--", "/bin/sh", "-c",
                  "echo forked | /bin/cat")
        assert ok.returncode == 0, ok.stderr
        assert ok.stdout.strip() == "forked"

        # rc is not pinned: a failed task (1) or an internal error (2) depending
        # on when the cap takes force. Either way it must not succeed.
        capped = _run("--processes=1", "--run", "--", "/bin/sh", "-c",
                      "echo forked | /bin/cat")
        assert capped.returncode != 0, (
            "--processes=1 must not admit a forking pipeline; "
            f"got rc=0 with stdout {capped.stdout!r}"
        )
    finally:
        _run("--cleanup")


def _stage_probe(box_root: Path, name: str) -> None:
    # Copy a built workload into box/ so the task can exec it without a --dir
    # rule; --run flips box/ to box_uid, so the box user can read and exec it.
    src = PROBES_DIR / name
    dst = box_root / "box" / name
    shutil.copy(src, dst)
    os.chmod(dst, 0o755)


def test_compat_no_stack_limit_means_unlimited():
    # #22: with no --stack, Isolate sets RLIMIT_STACK to RLIM_INFINITY rather
    # than leaving the caller's limit in place (isolate/isolate.c:803) — and the
    # Worker omits --stack whenever its stack-size limit is 0, its default. The
    # probe consumes ~32 MiB of stack, past the caller's usual 8 MiB, so it only
    # survives if the unlimited stack was actually applied.
    if not (PROBES_DIR / "deep_stack_test").exists():
        pytest.skip("deep_stack_test workload not built")
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        _stage_probe(Path(init.stdout.strip()), "deep_stack_test")

        run = _run("--run", "--", "/box/deep_stack_test")
        assert run.returncode == 0, run.stderr
        assert run.stdout.strip() == "deep-ok"
    finally:
        _run("--cleanup")


def test_compat_stack_limit_still_bites():
    # Companion: an explicit cap is still enforced, so unlimited-by-default did
    # not disable the limit. 1 MiB is far below the probe's ~32 MiB.
    if not (PROBES_DIR / "deep_stack_test").exists():
        pytest.skip("deep_stack_test workload not built")
    try:
        init = _run("--init")
        assert init.returncode == 0, init.stderr
        _stage_probe(Path(init.stdout.strip()), "deep_stack_test")

        run = _run("--stack=1024", "--run", "--", "/box/deep_stack_test")
        assert run.returncode != 0, (
            "--stack=1024 (KB) must not admit a ~32 MiB stack; "
            f"got rc=0 with stdout {run.stdout!r}"
        )
    finally:
        _run("--cleanup")


def test_meta_file_internal_error_is_xx(tmp_path):
    # A --run on an un-init'd box is an Guardian-internal error: exit 2 and a
    # status:XX meta with a message, written by root's terminate() meta-sink
    # (ADR 0005 C2).
    meta = tmp_path / "meta.txt"
    _run("--cleanup")  # ensure no prior state
    run = _run("--run", "--", "/bin/true", meta=str(meta))
    assert run.returncode == 2
    parsed = _parse_meta(meta)
    assert parsed.get("status") == "XX"
    assert "message" in parsed


# --- pinning: --cpuset-cpus / --cpuset-mems -----------------------
#
# CPUs are picked from this process's own affinity, not from every online CPU:
# on kernels >= 6.2 a caller's sched_setaffinity mask is intersected with the
# box's cpuset, so a test runner under `taskset` would otherwise see narrower
# sets than it asked for.

INSTANCE_CG = Path("/sys/fs/cgroup/recodex-guardian") / str(BOX_ID)


def _parse_cpu_list(text: str) -> set[int]:
    """Expand a kernel cpu/node list (`0-2,5`) into a set of ids."""
    ids: set[int] = set()
    for item in filter(None, text.strip().split(",")):
        lo, _, hi = item.partition("-")
        ids.update(range(int(lo), int(hi or lo) + 1))
    return ids


def _status_field(status: str, key: str) -> set[int]:
    """The id set on a /proc/<pid>/status line such as `Cpus_allowed_list:`."""
    for line in status.splitlines():
        if line.startswith(f"{key}:"):
            return _parse_cpu_list(line.split(":", 1)[1])
    raise AssertionError(f"{key} missing from status:\n{status}")


def _available_cpus() -> list[int]:
    return sorted(os.sched_getaffinity(0))


def _box_status(*flags, pid="self") -> str:
    """`/proc/<pid>/status` as seen by a task in a --run with extra flags."""
    run = _run("--run", *flags, "--", "/bin/cat", f"/proc/{pid}/status")
    assert run.returncode == 0, run.stderr
    return run.stdout


def test_cpuset_cpus_pins_the_task():
    cpus = _available_cpus()
    sets = [[cpus[0]]] + ([cpus[:2]] if len(cpus) >= 2 else [])
    try:
        assert _run("--init").returncode == 0
        for want in sets:
            flag = "--cpuset-cpus=" + ",".join(map(str, want))
            status = _box_status(flag)
            assert _status_field(status, "Cpus_allowed_list") == set(want)
    finally:
        _run("--cleanup")


def test_cpuset_mems_pins_the_task():
    try:
        assert _run("--init").returncode == 0
        status = _box_status("--cpuset-mems=0")
        assert _status_field(status, "Mems_allowed_list") == {0}
    finally:
        _run("--cleanup")


def test_cpuset_pins_the_proxy():
    # The whole box is pinned, not only the task: the proxy is PID 1 in the
    # box's PID namespace and must carry the same set.
    cpu = _available_cpus()[0]
    try:
        assert _run("--init").returncode == 0
        status = _box_status(f"--cpuset-cpus={cpu}", pid=1)
        assert _status_field(status, "Cpus_allowed_list") == {cpu}
    finally:
        _run("--cleanup")


def test_cpuset_pins_the_root_supervisor():
    # The root process sits in the instance cgroup's `leaf` while the task
    # runs; peek at it from outside during a sleep.
    cpu = _available_cpus()[0]
    leaf_procs = INSTANCE_CG / "leaf" / "cgroup.procs"
    try:
        assert _run("--init").returncode == 0
        proc = subprocess.Popen(
            ["sudo", str(GUARDIAN_BIN), f"--box-id={BOX_ID}", "--run",
             f"--cpuset-cpus={cpu}", "--", "/bin/sleep", "3"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            deadline = time.monotonic() + 2
            pids: list[str] = []
            while time.monotonic() < deadline and not pids:
                try:
                    pids = leaf_procs.read_text().split()
                except OSError:
                    pass
                time.sleep(0.05)
            assert pids, "root supervisor never appeared in the leaf cgroup"
            for pid in pids:
                status = Path(f"/proc/{pid}/status").read_text()
                assert _status_field(status, "Cpus_allowed_list") == {cpu}
        finally:
            _, err = proc.communicate(timeout=10)
        assert proc.returncode == 0, err
    finally:
        _run("--cleanup")


def test_cpuset_rejected_by_kernel_is_xx(tmp_path):
    # Well-formed lists naming a CPU / node the machine can never have: the
    # kernel refuses the write, and the run is a Guardian-internal error.
    for i, flag in enumerate(("--cpuset-cpus=100000", "--cpuset-mems=100000")):
        meta = tmp_path / f"meta{i}.txt"
        try:
            assert _run("--init").returncode == 0
            run = _run("--run", flag, "--", "/bin/true", meta=str(meta))
            assert run.returncode == 2, (flag, run.stderr)
            parsed = _parse_meta(meta)
            assert parsed.get("status") == "XX", (flag, parsed)
            assert "Failed to pin" in parsed.get("message", ""), parsed
        finally:
            _run("--cleanup")


def test_cpuset_offline_cpu_is_xx(tmp_path):
    # cgroup v2 accepts a possible-but-offline CPU (e.g. an SMT sibling after
    # smt/control=off) and would silently run the box on the parent's set; we
    # refuse to run instead. Needs a host that has such a CPU.
    sysfs = Path("/sys/devices/system/cpu")
    offline = _parse_cpu_list((sysfs / "possible").read_text()) - \
        _parse_cpu_list((sysfs / "online").read_text())
    if not offline:
        pytest.skip("no possible-but-offline CPU on this host")
    meta = tmp_path / "meta.txt"
    try:
        assert _run("--init").returncode == 0
        run = _run("--run", f"--cpuset-cpus={min(offline)}", "--", "/bin/true",
                   meta=str(meta))
        assert run.returncode == 2, run.stderr
        parsed = _parse_meta(meta)
        assert parsed.get("status") == "XX", parsed
        assert "granted" in parsed.get("message", ""), parsed
    finally:
        _run("--cleanup")


def test_cpuset_does_not_leak_into_the_next_run():
    # The instance cgroup is rebuilt on every --run, so pinning is per run:
    # an unpinned --run after a pinned one in the same box is unpinned.
    cpus = _available_cpus()
    if len(cpus) < 2:
        pytest.skip("needs at least 2 CPUs")
    try:
        assert _run("--init").returncode == 0
        pinned = _box_status(f"--cpuset-cpus={cpus[0]}")
        assert _status_field(pinned, "Cpus_allowed_list") == {cpus[0]}
        unpinned = _status_field(_box_status(), "Cpus_allowed_list")
        assert unpinned >= set(cpus), unpinned
    finally:
        _run("--cleanup")
