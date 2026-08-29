#!/usr/bin/env bash
# Undo host-setup.sh, restoring the values it captured.
#
# Restores from the snapshot rather than from assumed defaults, so a machine
# that was already unusual before the campaign is put back the way it was.
# If the snapshot is gone (e.g. after a reboot cleared /run) there is nothing
# to restore -- the reboot already reverted every runtime change.
set -euo pipefail

STATE=/run/guardian-perf/host-state.env

QUIET_SERVICES=(earlyoom avahi-daemon bluetooth firewalld udevmon)
QUIET_TIMERS=(plocate-updatedb man-db fstrim paccache logrotate shadow
              systemd-tmpfiles-clean archlinux-keyring-wkd-sync)

[[ $EUID -eq 0 ]] || { echo "must run as root: sudo $0" >&2; exit 1; }

if [[ ! -f $STATE ]]; then
    cat >&2 <<MSG
No snapshot at $STATE.

Every change host-setup.sh makes is runtime-only, so if the machine has
rebooted since setup it is already back to normal and there is nothing to do.
Unmasking anyway, in case setup ran since the last boot without a snapshot:
MSG
    for u in power-profiles-daemon "${QUIET_SERVICES[@]}"; do
        systemctl unmask --runtime "$u" 2>/dev/null || true
    done
    for t in "${QUIET_TIMERS[@]}"; do
        systemctl unmask --runtime "$t.timer" 2>/dev/null || true
    done
    exit 0
fi

# shellcheck disable=SC1090
source "$STATE"

# ------------------------------------------------------- frequency & SMT
echo on > /sys/devices/system/cpu/smt/control      # bring siblings back first
for p in /sys/devices/system/cpu/cpufreq/policy*; do
    echo "$GOVERNOR" > "$p/scaling_governor"
    [[ -w "$p/energy_performance_preference" && $EPP != "-" ]] &&
        echo "$EPP" > "$p/energy_performance_preference"
done
echo "$BOOST" > /sys/devices/system/cpu/cpufreq/boost

sysctl -q -w kernel.perf_event_paranoid="$PARANOID"

# --------------------------------------------------------------- services
# Unmask everything, then restart only what was actually running before.
for s in power-profiles-daemon "${QUIET_SERVICES[@]}"; do
    systemctl unmask --runtime "$s" 2>/dev/null || true
    var="SVC_${s//-/_}"
    [[ ${!var:-} == active ]] && { systemctl start "$s" 2>/dev/null || true; }
done
for t in "${QUIET_TIMERS[@]}"; do
    systemctl unmask --runtime "$t.timer" 2>/dev/null || true
    var="TMR_${t//-/_}"
    [[ ${!var:-} == active ]] && { systemctl start "$t.timer" 2>/dev/null || true; }
done

rm -f "$STATE"

echo
echo "===== restored ====="
printf "%-14s %s\n" governor "$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
printf "%-14s %s\n" boost    "$(cat /sys/devices/system/cpu/cpufreq/boost)"
printf "%-14s %s\n" smt      "$(cat /sys/devices/system/cpu/smt/control)"
printf "%-14s %s\n" "online cpus" "$(cat /sys/devices/system/cpu/online)"
printf "%-14s %s\n" paranoid "$(sysctl -n kernel.perf_event_paranoid)"
printf "%-14s %s\n" ppd      "$(systemctl is-active power-profiles-daemon 2>/dev/null || true)"
