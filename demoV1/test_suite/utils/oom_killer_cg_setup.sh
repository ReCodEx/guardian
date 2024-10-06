#!/bin/sh
set -eu

BYTES=1000000
CG_ROOT="/sys/fs/cgroup"
CG_PATH="${CG_ROOT}/$1"
mkdir $CG_PATH
echo $BYTES > "${CG_PATH}/memory.max"
echo "max" > "${CG_PATH}/memory.min"

