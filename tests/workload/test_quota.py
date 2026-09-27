"""Disk-quota workload tests (#19): `--quota=<blocks>,<inodes>` in compatibility
mode, and standalone `disk-usage`.

A quota needs a filesystem that enforces user quotas, which most dev and CI box
trees are not on. The positive tests therefore run on the real box tree when it
already enforces them, and otherwise on a small ext4 image loop-mounted with
`usrquota` over it for this module. The negative tests always mount an image of
their own — one without quota support, one that records limits without enforcing
them — since a quota-enforcing host cannot stand in for either.

Probes run in the box as the box user and write into /box, which lives on the
box-tree filesystem; a write past a cap fails with EDQUOT ("Disk quota
exceeded"), so the task exits non-zero.
"""

import os
import shutil
import subprocess
from contextlib import contextmanager
from pathlib import Path

import pytest

from conftest import GUARDIAN_BIN, RES_DIR, run_standalone

BOX_ID = 0
BOXES = Path("/var/lib/recodex-guardian/boxes")

# Test images are named with this prefix, so a mount left by an interrupted run
# is recognisable as ours and can be removed before the next one.
IMAGE_PREFIX = "guardian-quota-"
IMAGE_SIZE = 64 * 1024 * 1024
SBIN_PATH = os.pathsep.join(["/usr/sbin", "/sbin", os.environ.get("PATH", "")])

EDQUOT = "Disk quota exceeded"


# --- running the Guardian ----------------------------------------------------


def _run(*args, meta=None):
    cmd = ["sudo", str(GUARDIAN_BIN), f"--box-id={BOX_ID}"]
    if meta:
        cmd.append(f"--meta={meta}")
    cmd.extend(args)
    return subprocess.run(cmd, capture_output=True, text=True)


def _init(*quota):
    init = _run("--init", *(f"--quota={q}" for q in quota))
    assert init.returncode == 0, init.stderr


def _write_kib(kib, *extra, name="blob"):
    """`--run` a probe writing @p kib KiB into /box/<name>."""
    return _run(*extra, "--run", "--", "/bin/sh", "-c",
                f"head -c {kib * 1024} /dev/zero > /box/{name}")


def _create_files(count, *extra):
    """`--run` a probe creating @p count empty files in /box."""
    return _run(*extra, "--run", "--", "/bin/sh", "-c",
                f"i=0; while [ $i -lt {count} ]; do "
                f": > /box/f$i || exit 1; i=$((i + 1)); done")


def _parse_meta(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text().splitlines():
        if ":" in line:
            k, _, v = line.partition(":")
            out[k.strip()] = v.strip()
    return out


# --- the filesystem under the box tree ---------------------------------------


def _sudo(*cmd, check=True):
    return subprocess.run(["sudo", *cmd], capture_output=True, text=True,
                          check=check)


def _mount_holding(path: Path) -> dict:
    """The mount the box tree lives on (the topmost at its mount point), from
    /proc/self/mountinfo; per-mount and superblock options are merged."""
    while not path.exists():
        path = path.parent
    path = path.resolve()
    best = None
    for line in Path("/proc/self/mountinfo").read_text().splitlines():
        pre, _, post = line.partition(" - ")
        fields = pre.split()
        _fstype, source, super_opts = post.split()[:3]
        mnt = Path(fields[4])
        if path != mnt and mnt not in path.parents:
            continue
        if best is None or len(mnt.parts) >= len(best["mnt"].parts):
            best = {
                "mnt": mnt,
                "source": source,
                "opts": set(fields[5].split(",")) | set(super_opts.split(",")),
            }
    return best


def _is_test_image(source: str) -> bool:
    if not source.startswith("/dev/loop"):
        return False
    backing = Path("/sys/block") / Path(source).name / "loop" / "backing_file"
    try:
        return Path(backing.read_text().strip()).name.startswith(IMAGE_PREFIX)
    except OSError:
        return False


def _unmount_stale_images():
    """Remove test images an interrupted earlier run left over the box tree."""
    while True:
        m = _mount_holding(BOXES)
        if m["mnt"] != BOXES or not _is_test_image(m["source"]):
            return
        _sudo("umount", str(BOXES))


@contextmanager
def _image_over_boxes(tmp_dir: Path, name: str, *, quota_feature: bool,
                      usrquota: bool):
    """Loop-mount a fresh ext4 image over the box tree for the duration.

    @p quota_feature builds it with ext4's `quota` feature (usage accounting);
    @p usrquota mounts it with the option that makes the kernel enforce limits.
    """
    mkfs = shutil.which("mkfs.ext4", path=SBIN_PATH)
    if mkfs is None:
        pytest.skip("mkfs.ext4 not available to build a quota test image")

    image = tmp_dir / f"{IMAGE_PREFIX}{name}.img"
    with open(image, "wb") as f:
        f.truncate(IMAGE_SIZE)
    features = ["-O", "quota"] if quota_feature else []
    subprocess.run([mkfs, "-q", "-F", *features, str(image)], check=True,
                   capture_output=True)

    _sudo("mkdir", "-p", str(BOXES))
    options = "loop,usrquota" if usrquota else "loop"
    mount = _sudo("mount", "-o", options, str(image), str(BOXES), check=False)
    if mount.returncode != 0:
        pytest.skip(f"cannot loop-mount a test image over {BOXES}: "
                    f"{mount.stderr.strip()}")
    try:
        yield
    finally:
        _run("--cleanup")
        _sudo("umount", str(BOXES), check=False)


@pytest.fixture(scope="module")
def quota_boxes(tmp_path_factory):
    """The box tree on a filesystem that enforces user quotas: the real one when
    it already does, else an ext4 image mounted with `usrquota` over it."""
    _unmount_stale_images()
    if "usrquota" in _mount_holding(BOXES)["opts"]:
        yield
        # The cap outlives --cleanup; clear it off the host filesystem.
        _run("--init")
        _run("--cleanup")
        return
    with _image_over_boxes(tmp_path_factory.mktemp("quota"), "enforced",
                           quota_feature=True, usrquota=True):
        yield


@pytest.fixture
def box():
    """Clean up the test box after the test, whatever it did."""
    _run("--cleanup")
    yield
    _run("--cleanup")


# --- a cap is enforced or the phase fails ------------------------------------


def test_init_refuses_quota_on_filesystem_without_quotas(tmp_path, box):
    with _image_over_boxes(tmp_path, "noquota", quota_feature=False,
                           usrquota=False):
        init = _run("--init", "--quota=1024,100")
        assert init.returncode == 2
        assert "quotas have not been enabled" in init.stderr

        # Asking for no cap needs no quota filesystem: there is nothing to
        # clear on one that keeps no quotas.
        _run("--cleanup")
        assert _run("--init").returncode == 0


def test_init_refuses_quota_that_is_not_enforced(tmp_path, box):
    # ext4's quota feature without the usrquota mount option: quotactl records
    # the limits and returns 0, but the kernel never enforces them.
    with _image_over_boxes(tmp_path, "unenforced", quota_feature=True,
                           usrquota=False):
        init = _run("--init", "--quota=1024,100")
        assert init.returncode == 2
        assert "does not enforce them" in init.stderr


def test_run_quota_failure_is_an_internal_error(tmp_path, box):
    # --run sets --quota in the root process, so a failure is the Guardian's
    # own (status:XX), not a task that ran and failed.
    with _image_over_boxes(tmp_path, "noquota-run", quota_feature=False,
                           usrquota=False):
        _init()
        meta = tmp_path / "meta"
        run = _run("--quota=1024,100", "--run", "--", "/bin/true", meta=meta)
        assert run.returncode == 2
        assert _parse_meta(meta).get("status") == "XX"


# --- block and inode caps ----------------------------------------------------


def test_write_under_the_block_cap_succeeds(quota_boxes, box):
    _init("1024,100")
    run = _write_kib(256)
    assert run.returncode == 0, run.stderr


def test_write_over_the_block_cap_fails(quota_boxes, box):
    _init("1024,100")
    run = _write_kib(2048)
    assert run.returncode == 1
    assert EDQUOT in run.stderr


def test_zero_blocks_leaves_only_the_inode_cap(quota_boxes, box):
    # --quota=0,N still caps inodes, where Isolate would set no quota at all.
    _init("0,20")
    big = _write_kib(4096)
    assert big.returncode == 0, big.stderr
    many = _create_files(50)
    assert many.returncode == 1
    assert EDQUOT in many.stderr


def test_zero_inodes_leaves_only_the_block_cap(quota_boxes, box):
    # What the Worker sends when its disk-files is unset.
    _init("1024,0")
    many = _create_files(50)
    assert many.returncode == 0, many.stderr
    big = _write_kib(2048)
    assert big.returncode == 1
    assert EDQUOT in big.stderr


# --- when the cap changes ----------------------------------------------------


def test_run_quota_changes_the_cap_for_later_runs(quota_boxes, box):
    _init("1024,100")
    raised = _write_kib(4096, "--quota=8192,100", name="first")
    assert raised.returncode == 0, raised.stderr

    # No --quota: the raised cap stays. The first blob counts again (the box
    # tree is the box user's for every run), so this fits under 8 MiB only.
    kept = _write_kib(1024, name="second")
    assert kept.returncode == 0, kept.stderr


def test_init_without_quota_clears_an_earlier_cap(quota_boxes, box):
    # The cap is kept against the box user, so it outlives --cleanup.
    _init("1024,100")
    assert _run("--cleanup").returncode == 0

    _init()
    run = _write_kib(2048)
    assert run.returncode == 0, run.stderr


# --- standalone disk-usage ---------------------------------------------------


def test_standalone_small_disk_usage_rounds_up_to_a_real_cap(quota_boxes, box):
    # 500 bytes is one 1 KiB quota block, not 0 (unlimited). The probe writes
    # into the box's own /tmp scratch dir, which is on the box-tree filesystem;
    # /res is bound from the host. The fixed id keeps the cap on the test box's
    # uid, where the fixtures clear it.
    config = {
        "id": BOX_ID,
        "box-fs": {"dir-rules": [f"res={RES_DIR}:rw"]},
        "tasks": [{
            "task-id": "disk-usage",
            "stats-yaml": "/res/disk-usage.result.yaml",
            "cmd": {"bin": "/bin/sh",
                    "args": ["-c", "head -c 8192 /dev/zero > /tmp/blob"]},
            "limits": {"disk-usage": 500, "wall-time": 10},
        }],
    }
    assert run_standalone(config) == {"disk-usage": "non zero exit code"}
