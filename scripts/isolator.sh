#!/bin/sh
# One entry point for the Isolator's artifact lifecycle: build it, package it,
# install it, remove it, and tear its host-wide state down (see docs/adr/0009).
#
# Two things about this script are deliberate and easy to "fix" by mistake:
#
#   * The build tree's CMakeCache is the memory for configure-time options
#     (alias, prefix, build type). Modes inherit whatever the tree was configured
#     with and only reconfigure when a flag actually changes something, so
#     `build --alias` followed by a plain `install` still installs the alias.
#   * We produce RPMs but never install or remove them. `uninstall` refuses to
#     touch files an RPM owns, because deleting them behind rpm's back leaves the
#     rpm database broken. Use `dnf remove isolator` there.
#
# Usage:
#   isolator.sh build [--dev]            configure + build
#   isolator.sh package [--alias]        + cpack -G RPM (artifact only)
#   isolator.sh install [--alias]        + cmake --install (needs root)
#   isolator.sh uninstall [--purge]      remove installed files (needs root)
#   isolator.sh purge                    box tree + shared cgroup (needs root)
set -eu

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPO="$(dirname "$SCRIPT_DIR")"

# Host-wide state, matching the paths the binary itself uses.
BOX_TREE="/var/lib/isolator_boxes"
CGROUP_PARENT="/sys/fs/cgroup/isolator_boxes"

# Defaults, uniform across every mode so crossing from build to install never
# forces a reconfigure. Release (not empty/Debug) is what turns _FORTIFY_SOURCE
# on — src/CMakeLists.txt gates it to optimized configs — and is what the RPM
# expects, since CPack emits no -debuginfo subpackage to split symbols into.
BUILD_DIR="$REPO/build"
BUILD_TYPE="Release"
PREFIX="/usr"
ALIAS="OFF"
TESTING="OFF"
DESTDIR=""

# Which options the caller actually named: only these override the cache.
set_type=false
set_prefix=false
set_alias=false
set_testing=false

purge_too=false
dry_run=false

usage() {
    cat <<EOF
Usage: $(basename "$0") MODE [OPTIONS]

Modes:
  build       configure (if needed) + build
  package     build, then cpack -G RPM — produces the package, installs nothing
  install     build, then cmake --install
  uninstall   remove the files install put there (reads install_manifest.txt)
  purge       tear down $BOX_TREE and $CGROUP_PARENT

Options:
      --alias / --no-alias  install the \`isolate\` alias (default: no-alias)
      --prefix DIR          install prefix (default: $PREFIX)
      --type TYPE           CMake build type (default: $BUILD_TYPE)
      --testing             build the test tiers (-DTESTING=ON)
      --dev                 shorthand for --type Debug --testing
      --build-dir DIR       build tree (default: ./build)
      --destdir DIR         install: stage under DIR instead of installing live
                            (needs no root — everything, including the box tree,
                            lands under DIR; for packagers and dry checks)
      --purge               uninstall: also tear down host-wide state
  -n, --dry-run             uninstall/purge: print what would go, remove nothing
  -h, --help                this message

Configure-time options are remembered in the build tree, so later modes inherit
them; passing one again reconfigures and says so. RPM-owned installs are dnf's:
uninstall refuses to touch them.
EOF
}

# --- helpers ---------------------------------------------------------------

# Root for the privileged step only — never for configure/build, which would
# leave a root-owned build tree behind.
as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

say() { printf '%s\n' "$*"; }
die() { printf '%s: %s\n' "$(basename "$0")" "$*" >&2; exit 1; }

# Name of the RPM owning a path, or nothing. `rpm -qf` prints "file ... is not
# owned by any package" on *stdout* and exits 1, so the exit status — not the
# output — is what distinguishes owned from unowned.
rpm_owner() {
    command -v rpm >/dev/null 2>&1 || return 0
    if out=$(rpm -qf --queryformat '%{NAME}' "$1" 2>/dev/null); then
        printf '%s\n' "$out"
    fi
}

# Staged trees belong to the invoking user; only live paths need privilege.
rm_target() {
    if [ -n "$DESTDIR" ]; then
        rm -f "$1"
    else
        as_root rm -f "$1"
    fi
}

# Read one variable out of the build tree's cache; empty if absent.
cache_get() {
    [ -f "$BUILD_DIR/CMakeCache.txt" ] || return 0
    sed -n "s|^$1:[A-Z]*=\(.*\)$|\1|p" "$BUILD_DIR/CMakeCache.txt" | head -1
}

# --- modes -----------------------------------------------------------------

# Configure only when the tree is absent or an effective value differs from what
# it was configured with, then build. Values the caller didn't name are inherited
# from the cache, so a plain `install` never silently flips an earlier choice.
configure_and_build() {
    if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
        c_type=$(cache_get CMAKE_BUILD_TYPE)
        c_prefix=$(cache_get CMAKE_INSTALL_PREFIX)
        c_alias=$(cache_get ISOLATE_ALIAS)
        c_testing=$(cache_get TESTING)

        $set_type    || BUILD_TYPE="${c_type:-$BUILD_TYPE}"
        $set_prefix  || PREFIX="${c_prefix:-$PREFIX}"
        $set_alias   || ALIAS="${c_alias:-$ALIAS}"
        $set_testing || TESTING="${c_testing:-$TESTING}"

        changes=""
        [ "$BUILD_TYPE" = "$c_type" ]   || changes="$changes CMAKE_BUILD_TYPE:$c_type->$BUILD_TYPE"
        [ "$PREFIX" = "$c_prefix" ]     || changes="$changes CMAKE_INSTALL_PREFIX:$c_prefix->$PREFIX"
        [ "$ALIAS" = "$c_alias" ]       || changes="$changes ISOLATE_ALIAS:$c_alias->$ALIAS"
        [ "$TESTING" = "$c_testing" ]   || changes="$changes TESTING:$c_testing->$TESTING"

        if [ -n "$changes" ]; then
            say "reconfiguring:$changes"
        else
            say "build tree: type=$BUILD_TYPE prefix=$PREFIX alias=$ALIAS testing=$TESTING (no reconfigure)"
        fi
    else
        changes="new"
        say "configuring: type=$BUILD_TYPE prefix=$PREFIX alias=$ALIAS testing=$TESTING"
    fi

    if [ "$(id -u)" -eq 0 ]; then
        say "warning: building as root — build artifacts will be root-owned"
    fi

    [ -z "$changes" ] || cmake -S "$REPO" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DISOLATE_ALIAS="$ALIAS" \
        -DTESTING="$TESTING"

    cmake --build "$BUILD_DIR" -j"$(nproc)"
}

do_install() {
    configure_and_build

    # A DESTDIR install stages everything — including the absolute
    # /var/lib/isolator_boxes — under that root, so it needs no privilege. Only a
    # live install does.
    if [ -n "$DESTDIR" ]; then
        DESTDIR="$DESTDIR" cmake --install "$BUILD_DIR"
    else
        as_root cmake --install "$BUILD_DIR"
    fi

    bindir="$DESTDIR$(cache_get CMAKE_INSTALL_PREFIX)/bin"
    say "installed $bindir/isolator"
    if [ "$(cache_get ISOLATE_ALIAS)" = "ON" ]; then
        say "installed $bindir/isolate -> isolator"
    fi
    # The setuid bit is applied by install(PERMISSIONS) and a restrictive umask can
    # file it off, so report what actually landed rather than what we asked for.
    say "mode: $(ls -l "$bindir/isolator" | cut -d' ' -f1) (want -rwsr-xr-x)"
}

do_package() {
    configure_and_build
    ( cd "$BUILD_DIR" && cpack -G RPM )
    say "packaged: $(ls "$BUILD_DIR"/isolator-*.rpm 2>/dev/null | tr '\n' ' ')"
    say "install it with dnf; this script deliberately does not (docs/adr/0009)"
}

do_uninstall() {
    manifest="$BUILD_DIR/install_manifest.txt"

    # Survey the *host* before demanding a manifest. An RPM install has no manifest
    # anywhere, so leading with the manifest answers "no install_manifest.txt" to
    # someone whose isolator came from dnf — true, and useless. Two installs can
    # also coexist (an unowned /usr/local one shadowing an RPM /usr one on PATH),
    # and each is removed a different way.
    live_rpm=""   # "path=package" for RPM-owned installs
    live_src=""   # unowned paths, i.e. source installs
    seen=""
    if [ -z "$DESTDIR" ]; then
        for cand in "$(command -v isolator 2>/dev/null || true)" \
                    /usr/bin/isolator /usr/local/bin/isolator; do
            [ -n "$cand" ] && [ -e "$cand" ] || continue
            case "$seen" in *"|$cand|"*) continue ;; esac
            seen="$seen|$cand|"
            owner="$(rpm_owner "$cand")"
            if [ -n "$owner" ]; then
                live_rpm="${live_rpm}${live_rpm:+ }$cand=$owner"
            else
                live_src="${live_src}${live_src:+ }$cand"
            fi
        done
    fi

    if [ ! -f "$manifest" ]; then
        [ -n "$live_rpm$live_src" ] || \
            die "no $manifest, and no isolator in PATH, /usr/bin or /usr/local/bin — nothing to uninstall"

        say "$manifest is missing, so there is no record of a source install to undo." >&2
        say "What is installed here:" >&2
        for entry in $live_rpm; do
            say "  ${entry%=*}  — owned by the '${entry#*=}' RPM: sudo dnf remove ${entry#*=}" >&2
        done
        for path in $live_src; do
            say "  $path  — source install, unowned: re-run with --build-dir pointing at the" >&2
            say "      tree that installed it, or: sudo rm -f $path ${path%/*}/isolate" >&2
        done
        exit 1
    fi

    # A manifest-driven removal only undoes what this tree installed, so flag any
    # other, RPM-owned isolator rather than leaving the user to wonder why it stayed.
    for entry in $live_rpm; do
        say "note: ${entry%=*} is owned by the '${entry#*=}' RPM — dnf's to remove, not ours"
    done

    # CMake strips DESTDIR out of the manifest, so a staged install records the
    # *live* paths (/usr/bin/isolator, not <destdir>/usr/bin/isolator). Removing a
    # staged install therefore needs the same --destdir the install had, or we would
    # delete the real system's files instead.
    if [ -n "$DESTDIR" ]; then
        say "staged removal under $DESTDIR (manifest paths are DESTDIR-less by design)"
    fi

    # The manifest is authoritative for what cmake --install put down, with one
    # structural gap: the alias symlink comes from install(CODE ...), which CMake
    # never records. Derive its directory from the manifest's own isolator entry
    # so the prefix is inherited rather than guessed.
    bindir="$(sed -n 's|/isolator$||p' "$manifest" | head -1)"
    alias_link="${bindir:+$DESTDIR$bindir/isolate}"

    # The manifest may name a prefix the host probe above never looked at, so check
    # ownership there too: deleting RPM-owned files behind rpm's back leaves the
    # package database convinced isolator is still installed.
    if [ -z "$DESTDIR" ] && [ -n "$bindir" ]; then
        owner="$(rpm_owner "$bindir/isolator")"
        if [ -n "$owner" ]; then
            die "$bindir/isolator belongs to the '$owner' RPM — use: sudo dnf remove $owner"
        fi
    fi

    # `|| [ -n "$path" ]` is load-bearing: CMake writes the manifest with no
    # trailing newline, and a plain `read` loop drops that last entry.
    while IFS= read -r path || [ -n "$path" ]; do
        [ -n "$path" ] || continue
        target="$DESTDIR$path"
        if [ ! -e "$target" ]; then
            say "absent  $target"
        elif $dry_run; then
            say "would remove $target"
        else
            rm_target "$target"
            say "removed $target"
        fi
    done < "$manifest"

    if [ -n "$alias_link" ] && [ -L "$alias_link" ]; then
        if $dry_run; then
            say "would remove $alias_link (the alias, absent from the manifest)"
        else
            rm_target "$alias_link"
            say "removed $alias_link"
        fi
    fi

    # Host-wide state is not staged, so a DESTDIR removal stops here.
    if [ -n "$DESTDIR" ]; then
        return
    fi

    if $purge_too; then
        do_purge
        return
    fi

    # Mirror `dnf remove` on the package's %dir: reclaim the box tree only when
    # it is empty. A non-empty tree means live or leftover boxes, which are not
    # ours to destroy without being asked.
    if [ -d "$BOX_TREE" ]; then
        boxes=$(find "$BOX_TREE" -mindepth 1 -maxdepth 1 | wc -l)
        if [ "$boxes" -eq 0 ]; then
            if $dry_run; then
                say "would rmdir $BOX_TREE (empty)"
            else
                as_root rmdir "$BOX_TREE"
                say "removed $BOX_TREE (was empty)"
            fi
        else
            say "$BOX_TREE: $boxes box(es) present — left in place"
            say "  run '$(basename "$0") purge' to remove them and the cgroup parent"
        fi
    fi
}

do_purge() {
    # Purge is host-wide by definition; there is no staged equivalent.
    [ -z "$DESTDIR" ] || die "purge operates on host state — --destdir makes no sense here"
    if $dry_run; then
        say "would rmdir $CGROUP_PARENT (leaves first)"
        say "would rm -rf $BOX_TREE"
        return
    fi
    # Depth-first: a cgroup directory only rmdirs once its children are gone.
    if [ -d "$CGROUP_PARENT" ]; then
        as_root find "$CGROUP_PARENT" -type d -depth -exec rmdir {} \; || true
        say "tore down $CGROUP_PARENT"
    fi
    as_root rm -rf "$BOX_TREE"
    say "removed $BOX_TREE"
}

# --- argument parsing ------------------------------------------------------

[ $# -gt 0 ] || { usage; exit 1; }
case "$1" in
    -h|--help) usage; exit 0 ;;
    build|package|install|uninstall|purge) mode="$1"; shift ;;
    *) die "unknown mode '$1' (try --help)" ;;
esac

options=$(getopt -o nh --long alias,no-alias,prefix:,type:,testing,dev,build-dir:,destdir:,purge,dry-run,help -- "$@")
eval set -- "$options"

while true; do
    case "$1" in
        --alias)     ALIAS="ON";  set_alias=true ;;
        --no-alias)  ALIAS="OFF"; set_alias=true ;;
        --prefix)    PREFIX="$2"; set_prefix=true; shift ;;
        --type)      BUILD_TYPE="$2"; set_type=true; shift ;;
        --testing)   TESTING="ON"; set_testing=true ;;
        --dev)       BUILD_TYPE="Debug"; set_type=true; TESTING="ON"; set_testing=true ;;
        --build-dir) BUILD_DIR="$2"; shift ;;
        --destdir)   DESTDIR="$2"; shift ;;
        --purge)     purge_too=true ;;
        -n|--dry-run) dry_run=true ;;
        -h|--help)   usage; exit 0 ;;
        --) shift; break ;;
        *) die "unknown option $1" ;;
    esac
    shift
done

[ $# -eq 0 ] || die "unexpected argument '$1'"

case "$mode" in
    build)     configure_and_build ;;
    package)   do_package ;;
    install)   do_install ;;
    uninstall) do_uninstall ;;
    purge)     do_purge ;;
esac
