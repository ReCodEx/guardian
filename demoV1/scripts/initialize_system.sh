#!/bin/sh
set -eu
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
sudo mkdir /sys/fs/cgroup/isolate_boxes || true
echo "+cpu +memory +pids" | sudo tee /sys/fs/cgroup/cgroup.subtree_control
echo "+cpu +memory +pids" | sudo tee /sys/fs/cgroup/isolate_boxes/cgroup.subtree_control