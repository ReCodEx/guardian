#!/bin/sh
set -eu

usage() {
        cat <<EOF
Usage: $(basename "$0") [OPTIONS] [YAML_FILE]

Options:
  -y, --yaml YAML_FILE    Path to YAML configuration file
  -h, --help              Display this help message and exit

The YAML file can be specified either with -y/--yaml option or as an unnamed argument.
EOF
}

use_yaml=false
yaml=""
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

# If YAML wasn't provided with -y option, check for unnamed argument
if [ "$use_yaml" = false ] && [ $# -gt 0 ]; then
    yaml="$1"
    use_yaml=true
    shift
fi

# Check if YAML file is provided
if [ "$use_yaml" = false ] || [ -z "$yaml" ]; then
    echo "Error: YAML configuration file is required." >&2
    usage
    exit 1
fi

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
sudo $SCRIPT_DIR/../build/src/isolator "--yaml=$yaml"