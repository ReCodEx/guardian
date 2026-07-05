"""Checkpoint B1: the --init / --cleanup loop (no --run yet).

Exercises the two phases that manage a box's on-disk lifecycle without running
anything inside it: --init creates the box tree and marks the box initialized,
--cleanup tears it down and clears the lock so the id is reusable (ADR 0005).
The full init -> run -> cleanup path lives in test_compat_phases.py, which stays
skipped until --run (B4) lands.
"""

import os
import subprocess
from pathlib import Path

from conftest import ISOLATOR_BIN

BOX_ID = 0
BOX_UID = BOX_ID + 60000  # box_uid = box_id + 60000 (CONTEXT.md: Credentials)


def _run(*args):
    return subprocess.run(
        ["sudo", str(ISOLATOR_BIN), f"--box-id={BOX_ID}", *args],
        capture_output=True,
        text=True,
    )


def _init_ok():
    init = _run("--init")
    assert init.returncode == 0, init.stderr
    # stdout is exactly the box root path, nothing else (logs go to stderr).
    box_root = Path(init.stdout.strip())
    assert init.stdout.strip() and "\n" not in init.stdout.strip()
    return box_root


def test_init_creates_tree_then_cleanup_removes_it():
    _run("--cleanup")  # clean slate (clears any leftover lock/tree)

    box_root = _init_ok()
    assert box_root.is_dir()

    # The writable working dir: 0777, owned box_uid:box_gid (CONTEXT.md).
    box = box_root / "box"
    st = box.stat()
    assert box.is_dir()
    assert st.st_mode & 0o777 == 0o777
    assert st.st_uid == BOX_UID and st.st_gid == BOX_UID
    # box_root itself stays root-owned.
    assert box_root.stat().st_uid == 0

    # --cleanup removes the tree and reports success.
    cu = _run("--cleanup")
    assert cu.returncode == 0, cu.stderr
    assert not box_root.exists()

    # --cleanup on an already-clean box is a no-op success (idempotent).
    assert _run("--cleanup").returncode == 0


def test_reinit_refused_until_cleanup():
    _run("--cleanup")

    _init_ok()
    # A second --init without an intervening --cleanup is refused (exit 2) —
    # our divergence from Isolate's silent rebuild (ADR 0005 refinement).
    assert _run("--init").returncode == 2

    # After --cleanup clears the lock, the same id can be --init'd again.
    assert _run("--cleanup").returncode == 0
    box_root = _init_ok()
    assert (box_root / "box").is_dir()

    _run("--cleanup")  # leave the box clean for the next test/session
