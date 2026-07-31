#!/bin/sh
# Bootstrap a local Python venv (idempotent) and run the workload-test tier.
#
# On Arch and other PEP-668 distros the system Python is externally managed, so
# we never pip-install globally: deps live in tests/requirements.txt and are
# installed into a gitignored .venv at the repo root. The workload tier drives
# the real isolator, which needs root, so pytest is launched under sudo (the
# venv's interpreter works fine as root — a venv is just a path layout).
#
# Usage:
#   scripts/workload_tests.sh            # bootstrap (if needed) + run all
#   scripts/workload_tests.sh -k limits  # forwards args to pytest
#   scripts/workload_tests.sh --co       # collect only (no sudo, see below)
set -eu

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPO="$SCRIPT_DIR/.."
VENV="$REPO/.venv"

# 1. Materialize the venv if absent.
if [ ! -x "$VENV/bin/pytest" ]; then
    echo "Creating venv at $VENV ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --upgrade -q pip
    "$VENV/bin/pip" install -q -r "$REPO/tests/requirements.txt"
fi

# 2. Run pytest. Collection/help need no root; an actual run does, so re-exec
#    under sudo unless we are already root.
cd "$REPO/tests/workload"
case " $* " in
    *" --co "*|*" --collect-only "*|*" -h "*|*" --help "*)
        exec "$VENV/bin/pytest" "$@" ;;
esac

if [ "$(id -u)" -eq 0 ]; then
    exec "$VENV/bin/pytest" "$@"
else
    exec sudo "$VENV/bin/pytest" "$@"
fi
