#!/bin/sh
set -eu

usage() {
        cat <<EOF
        Wrong usage
EOF
}

use_yaml=false
yaml=
options=$(getopt -o y:h --long yaml:,help -- "$@")
eval set -- "$options"

while true; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -y|--yaml)
            use_yaml=true
            yaml="$2"
            shift
            ;;
        --) # End of options
            shift
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
    shift
done

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
CWD=$(pwd)

mkdir $SCRIPT_DIR/../build || true
cd $SCRIPT_DIR/../build
cmake ..
cmake --build .
cd $CWD

sudo cgdelete -r memory:container_instance || true
sudo $SCRIPT_DIR/../build/src/container "--yaml=$yaml"