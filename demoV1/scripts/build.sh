#!/bin/sh
set -eu
SCRIPT_DIR="$(dirname "$(realpath "$0")")"

mkdir $SCRIPT_DIR/../build || true
cd $SCRIPT_DIR/../build
cmake ..
cmake --build .