#!/bin/sh
set -eu
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
echo "+cpu +memory +pids" | sudo tee /sys/fs/cgroup/cgroup.subtree_control


