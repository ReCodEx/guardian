#!/usr/bin/env python3
"""Capture the measurement platform's state as JSON.

Run at *collection* time, once per campaign, and keep the output beside the
data. Querying the platform from inside the analysis notebook instead would
report whatever the machine looks like when the notebook is re-rendered, which
can be months later and on different hardware.

Usage:  perf/harness/capture-platform.py [-o perf/data/platform.json]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def read(path: str | Path, default: str | None = None) -> str | None:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return default


def run(*cmd: str) -> str | None:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() or None


def hwmon(name: str) -> Path | None:
    for h in sorted(Path("/sys/class/hwmon").glob("hwmon*")):
        if read(h / "name") == name:
            return h
    return None


def cpu_topology() -> dict:
    cpus = {}
    for c in sorted(Path("/sys/devices/system/cpu").glob("cpu[0-9]*")):
        topo = c / "topology"
        if not topo.is_dir():
            continue
        cpus[c.name] = {
            "core_id": read(topo / "core_id"),
            "thread_siblings": read(topo / "thread_siblings_list"),
            "cur_freq_khz": read(c / "cpufreq/scaling_cur_freq"),
        }
    return cpus


def guardian_provenance() -> dict:
    """Identify exactly which binary produced the measurements."""
    binary = shutil.which("recodex-guardian") or str(REPO / "build/src/recodex-guardian")
    info: dict = {"binary": binary}
    p = Path(binary)
    if p.exists():
        data = p.read_bytes()
        info["sha256"] = hashlib.sha256(data).hexdigest()
        info["size_bytes"] = len(data)
        st = p.stat()
        info["mode"] = oct(st.st_mode & 0o7777)   # 4755 means the setuid install
        info["mtime"] = datetime.fromtimestamp(st.st_mtime, timezone.utc).isoformat()
    info["commit"] = run("git", "-C", str(REPO), "rev-parse", "HEAD")
    info["commit_subject"] = run("git", "-C", str(REPO), "log", "-1", "--format=%s")

    # Only uncommitted changes to the *build inputs* mean the binary fails to
    # correspond to a commit. Edits under perf/ or docs/ are expected
    # throughout a campaign, and warning about them would train the operator
    # to ignore the warning that matters.
    build_inputs = ("src/", "CMakeLists.txt", "scripts/")
    status = run("git", "-C", str(REPO), "status", "--porcelain") or ""
    dirty = [ln[3:] for ln in status.splitlines() if ln[3:].startswith(build_inputs)]
    info["build_inputs_dirty"] = dirty
    info["other_dirty_paths"] = len(status.splitlines()) - len(dirty)
    return info


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=str(REPO / "perf/data/platform.json"))
    args = ap.parse_args()

    k10 = hwmon("k10temp")
    cpufreq = "/sys/devices/system/cpu/cpufreq"

    snapshot = {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "host": platform.node(),
        "kernel": platform.release(),
        "distro": read("/etc/os-release", "").splitlines()[:3],
        "cpu": {
            "model": next((l.split(":", 1)[1].strip()
                           for l in (read("/proc/cpuinfo") or "").splitlines()
                           if l.startswith("model name")), None),
            "microcode": next((l.split(":", 1)[1].strip()
                               for l in (read("/proc/cpuinfo") or "").splitlines()
                               if l.startswith("microcode")), None),
            "online": read("/sys/devices/system/cpu/online"),
            "smt": read("/sys/devices/system/cpu/smt/control"),
            "topology": cpu_topology(),
        },
        "frequency": {
            "governor": read(f"{cpufreq}/policy0/scaling_governor"),
            "epp": read(f"{cpufreq}/policy0/energy_performance_preference"),
            "boost": read(f"{cpufreq}/boost"),
            "amd_pstate": read("/sys/devices/system/cpu/amd_pstate/status"),
            "min_khz": read("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq"),
            "max_khz": read("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"),
        },
        "kernel_tunables": {
            "perf_event_paranoid": read("/proc/sys/kernel/perf_event_paranoid"),
            "randomize_va_space": read("/proc/sys/kernel/randomize_va_space"),
            "nmi_watchdog": read("/proc/sys/kernel/nmi_watchdog"),
        },
        "memory": {
            "total_kb": next((l.split()[1] for l in (read("/proc/meminfo") or "").splitlines()
                              if l.startswith("MemTotal")), None),
            "swap": run("swapon", "--show", "--noheadings") or "none",
        },
        "storage": {"df_box_tree": run("df", "-hT", "/var/lib")},
        "thermal": {
            "k10temp_mdegC": read(k10 / "temp1_input") if k10 else None,
        },
        "toolchain": {
            "cc": run("cc", "--version"),
            "python": sys.version,
        },
        "guardian": guardian_provenance(),
        # Non-empty means something the setup script quiesces came back up.
        "unquiesced": [s for s in ("power-profiles-daemon", "earlyoom", "avahi-daemon",
                                   "bluetooth", "firewalld", "udevmon")
                       if run("systemctl", "is-active", s) == "active"],
    }

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(snapshot, indent=2) + "\n")
    print(f"wrote {out}")

    warn = []
    if snapshot["frequency"]["boost"] != "0":
        warn.append("CPU boost is ON")
    if snapshot["cpu"]["smt"] not in (None, "off", "notsupported"):
        warn.append("SMT is ON")
    if snapshot["kernel_tunables"]["perf_event_paranoid"] not in ("1", "0", "-1"):
        warn.append("perf_event_paranoid > 1: kernel-side counters unavailable")
    if snapshot["guardian"]["build_inputs_dirty"]:
        warn.append("uncommitted build inputs, binary matches no commit: "
                    + ", ".join(snapshot["guardian"]["build_inputs_dirty"]))
    if snapshot["guardian"].get("mode") not in (None, "0o4755"):
        warn.append(f"guardian is mode {snapshot['guardian']['mode']}, not the "
                    "setuid 4755 install: runs will need sudo and orig_uid will be root")
    if snapshot["unquiesced"]:
        warn.append("running again: " + ", ".join(snapshot["unquiesced"]))
    for w in warn:
        print(f"  WARNING: {w}", file=sys.stderr)
    return 1 if warn else 0


if __name__ == "__main__":
    raise SystemExit(main())
