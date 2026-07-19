#!/bin/sh
set -eu
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
sudo find /sys/fs/cgroup/isolator_boxes -type d -depth -exec rmdir {} \;
sudo rm -rf /var/lib/isolator_boxes