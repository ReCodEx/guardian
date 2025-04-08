#!/bin/sh
set -eu
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
sudo rm -rf /isolate_boxes
sudo find /sys/fs/cgroup/isolate_boxes -type d -depth -exec rmdir {} \;

