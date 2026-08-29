#!/usr/bin/env bash
# One-time measurement setup. Idempotent; re-run after any reboot.
#
# Every change here is runtime-only -- plain sysfs writes plus `--runtime`
# systemd properties -- so a reboot reverts the machine on its own. Forgetting
# host-teardown.sh is therefore recoverable rather than permanent.
set -euo pipefail

STATE_DIR=/run/guardian-perf
STATE="$STATE_DIR/host-state.env"

QUIET_SERVICES=(earlyoom avahi-daemon bluetooth firewalld udevmon)
QUIET_TIMERS=(plocate-updatedb man-db fstrim paccache logrotate shadow
              systemd-tmpfiles-clean archlinux-keyring-wkd-sync)

[[ $EUID -eq 0 ]] || { echo "must run as root: sudo $0" >&2; exit 1; }

hwmon_by_name() {
    local h
    for h in /sys/class/hwmon/hwmon*; do
        [[ -r "$h/name" && $(cat "$h/name") == "$1" ]] && { echo "$h"; return 0; }
    done
    return 1
}

# ---------------------------------------------------------------- snapshot
# Captured once. Re-running setup must not overwrite it with already-modified
# values, or teardown would restore the campaign settings as if they were
# the originals.
if [[ -f $STATE ]]; then
    echo "== prior state already captured at $STATE; re-applying settings"
else
    mkdir -p "$STATE_DIR"
    {
        echo "GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
        echo "EPP=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null || echo -)"
        echo "BOOST=$(cat /sys/devices/system/cpu/cpufreq/boost)"
        echo "SMT=$(cat /sys/devices/system/cpu/smt/control)"
        echo "PARANOID=$(sysctl -n kernel.perf_event_paranoid)"
        for s in power-profiles-daemon "${QUIET_SERVICES[@]}"; do
            echo "SVC_${s//-/_}=$(systemctl is-active "$s" 2>/dev/null || true)"
        done
        for t in "${QUIET_TIMERS[@]}"; do
            echo "TMR_${t//-/_}=$(systemctl is-active "$t.timer" 2>/dev/null || true)"
        done
    } > "$STATE"
    echo "== captured prior state to $STATE"
fi

# --------------------------------------- 1. stop whatever manages frequency
# power-profiles-daemon rewrites EPP and boost on its own schedule. Without
# masking it, every setting below is liable to be reverted mid-campaign.
systemctl stop power-profiles-daemon 2>/dev/null || true
systemctl mask --runtime power-profiles-daemon 2>/dev/null || true

# ------------------------------------------------------ 2. pin the frequency
for p in /sys/devices/system/cpu/cpufreq/policy*; do
    echo performance > "$p/scaling_governor"
    [[ -w "$p/energy_performance_preference" ]] &&
        echo performance > "$p/energy_performance_preference"
done
echo 0 > /sys/devices/system/cpu/cpufreq/boost   # base clock only

# ------------------------------------------------------------- 3. SMT off
# Largest single variance source, and it settles the sibling-mapping question.
# This part pairs siblings adjacently, so cpu1/3/5/7 go offline.
echo off > /sys/devices/system/cpu/smt/control

# ------------------------------------------- 4. let counters see kernel work
# At the default 2 an unprivileged task must set exclude_kernel=1, hiding
# exactly the kernel-side work being measured.
sysctl -q -w kernel.perf_event_paranoid=1

# ------------------------------------------------ 5. quiesce the background
# earlyoom is a hazard rather than noise: 7 GiB RAM, no swap, and a workload
# that deliberately touches many pages invites it to SIGKILL the subject.
for s in "${QUIET_SERVICES[@]}"; do
    systemctl stop "$s" 2>/dev/null || true
    systemctl mask --runtime "$s" 2>/dev/null || true
done

# plocate-updatedb walks the whole filesystem at 00:49; man-db reindexes;
# fstrim TRIMs the SSD. Any of them lands inside an overnight campaign.
for t in "${QUIET_TIMERS[@]}"; do
    systemctl stop "$t.timer" 2>/dev/null || true
    systemctl mask --runtime "$t.timer" 2>/dev/null || true
done

# ------------------------------------------------------------- 6. verify
echo
echo "===== achieved state ====="
printf "%-14s %s\n" governor "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
printf "%-14s %s\n" epp      "$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null || echo -)"
printf "%-14s %s (0 = off)\n" boost "$(cat /sys/devices/system/cpu/cpufreq/boost)"
printf "%-14s %s\n" smt      "$(cat /sys/devices/system/cpu/smt/control)"
printf "%-14s %s\n" "online cpus" "$(cat /sys/devices/system/cpu/online)"
printf "%-14s %s\n" paranoid "$(sysctl -n kernel.perf_event_paranoid)"
swap=$(swapon --show --noheadings 2>/dev/null || true)
printf "%-14s %s\n" swap "${swap:-none}"
if k=$(hwmon_by_name k10temp); then
    printf "%-14s %s m°C\n" "die temp" "$(cat "$k"/temp1_input)"
fi
printf "%-14s %s\n" "cur freqs" \
    "$(cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq 2>/dev/null | tr '\n' ' ')"
echo
echo "Frequencies should now read flat and near base clock. If they still vary,"
echo "something is re-managing cpufreq -- check power-profiles-daemon is masked."
