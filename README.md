# ReCoDex Isolator

Lightweight Linux containerization tool written from scratch for the ReCoDex assignment evaluation system. 

---

## 📚 Overview

This project uses advanced Linux kernel features (namespaces, cgroups, UID/GID mappings, etc.) to create secure sandboxes for running untrusted code. It provides fine-grained control over system resources and filesystem access. To get more insight into the details, you can take a look at my thesis in the `docs/thesis.pdf` file.

### 🧠 Terminology

- **Isolator** — This tool as a whole, providing containerization capabilities.
- **Instance** — One run of the isolator, from parsing the configuration file to executing tasks and generating metadata.
- **Sandbox** — The isolated environment created based on configuration, including namespaces, cgroups, UID/GID mappings, environment variables, and filesystem mounts.
- **Task** — A single unit of execution within the sandbox, running an executable with specified arguments and resource limits.

### 🧵 Process Architecture

An instance runs three different processes with distinct responsibilities:

- **Root process** — Reserves necessary global resources (directories, cgroups) and prepares the environment.
- **Proxy process** — Creates and configures the sandbox environment (namespaces, filesystem mounts, cgroups).
- **Task process** — Executes the isolated program with the specified resource limits.

---

## 🛠️ System Requirements

- Linux kernel with cgroupv2 enabled.
- CMake 3.20+ and a compiler supporting C++23 (on Rocky 9 that means
  `gcc-toolset-14`; the system GCC 11.5 is not enough)
- `libcap` development headers (`libcap-devel` / `libcap-dev`)
- Network access at configure time: yaml-cpp is fetched by CMake and statically
  linked. For an offline build, point `FETCHCONTENT_SOURCE_DIR_YAML-CPP` at a
  pre-fetched tree.
- `rpm-build` only if you want to produce the RPM
- For disk usage quotas, the sandbox must be on a filesystem supporting `QUOTACTL(2)` (e.g., ext4)

---

## 📦 Installation

One script drives the whole lifecycle — `scripts/isolator.sh MODE`:

| Mode | What it does |
| --- | --- |
| `build` | configure (only if needed) + build |
| `package` | build, then `cpack -G RPM` — produces the package, installs nothing |
| `install` | build, then `cmake --install` |
| `uninstall` | remove what `install` put there, alias included |
| `purge` | tear down `/var/lib/isolator_boxes` and the shared cgroup parent |

Defaults are the same for every mode: `Release`, prefix `/usr`, no `isolate`
alias, no test tiers. `scripts/isolator.sh --help` lists the flags.

> **On Rocky 9, enter the toolset first — for *every* mode that compiles:**
> ```sh
> scl enable gcc-toolset-14 -- bash
> ```
> The system compiler is GCC 11.5, which has no `<format>`; `src/logs.hpp` and
> `src/terminate.hpp` need it, so a build outside the toolset fails with
> `fatal error: format: No such file or directory`. Modern dev distros need
> nothing special.

### Option A — build in place, run with `sudo` (development)

No installation at all: build in the tree and invoke the binary through `sudo`.
This is what the workload and mock-evaluator test tiers use.

```sh
scripts/isolator.sh build                     # into ./build
sudo ./build/src/isolator --yaml=config.yml
```

Add `--dev` (`Debug` + `-DTESTING=ON`) when you want the test tiers built too —
the workload tier needs it, since a default build compiles no test code.
`scripts/isolator_run.sh config.yml` is a thin convenience wrapper for that
second line, if you tire of typing the `sudo` and the path.

Nothing is placed on the system; the only persistent state is the box tree
(`/var/lib/isolator_boxes`) and the shared cgroup parent, both created lazily on
first run and removable with `scripts/isolator.sh purge`.
Because the binary is not setuid here, every invocation needs root — the
Isolate-compatible drop-in path (a non-root caller) is *not* exercised by this
option.

### Option B — install from source (system-wide, no packaging)

Use this on hosts where you cannot or do not want to build an RPM. `install`
builds first, so this is the whole procedure from a clean tree:

```sh
scripts/isolator.sh install            # add --alias on a ReCodEx Worker host
```

It installs:

| Path | What |
| --- | --- |
| `/usr/bin/isolator` | the binary, **mode 4755 (setuid root)**|
| `/var/lib/isolator_boxes` | persistent box tree |
| `/usr/share/man/man1/isolator.1` | man page |

Only the `cmake --install` step is elevated (via `sudo`); configure and build
never are, so the build tree stays yours. The script prints the mode that
actually landed, because `cmake --install` applies permissions directly and a
restrictive umask can file the setuid bit off — expect `-rwsr-xr-x`. (The RPM in
Option C forces `4755` regardless of the build user's umask.)

Configure-time choices are remembered in the build tree, so `install` after a
`build --alias` still installs the alias; naming a flag again reconfigures and
says so. `--destdir DIR` stages the install under `DIR` instead, which needs no
root at all — useful for inspecting exactly what would land.

#### Uninstalling

```sh
scripts/isolator.sh uninstall          # -n / --dry-run to preview
```

Run it from the *same* build tree you installed from — it reads that tree's
`install_manifest.txt`, and errors out rather than guessing if the manifest is
gone. It removes one path the manifest does not contain: the `isolate` symlink,
which CMake never records because an `install(CODE ...)` step creates it. Left
behind, that symlink dangles at the front of `PATH` and anything invoking
`isolate` fails confusingly instead of falling through to another install.

The box tree is reclaimed only when empty, exactly as `dnf remove` treats the
package's directory. A non-empty tree means live or leftover boxes, so it is
reported and left alone; `uninstall --purge` (or `purge` on its own) removes it
and the shared cgroup parent.

On a host where the isolator came from an **RPM**, `uninstall` refuses and points
you at `dnf`: deleting RPM-owned files behind `rpm`'s back leaves the package
database convinced it is still installed. And when there's no manifest to work
from, it surveys the host instead of just complaining — listing every `isolator`
it can find with the right removal route for each, since an RPM install under
`/usr` and a source install under `/usr/local` can coexist (and the `/usr/local`
one wins on a default `PATH`).

### Option C — RPM package (el9, the production path)

The ReCodEx Worker cluster runs Rocky Linux 9 and its Worker RPM declares
`Requires: isolate`, which our package satisfies with `Provides: isolate` **when
built with the alias** — so a package destined for a Worker host wants `--alias`:

```sh
scl enable gcc-toolset-14 -- bash
scripts/isolator.sh package --alias    # -> build/isolator-0.1.0-1.el9.x86_64.rpm
sudo dnf install ./build/isolator-0.1.0-1.el9.x86_64.rpm
```

`package` produces the RPM and stops there; installing and removing it is `dnf`'s
job, deliberately. The default `Release` build type matters here: CPack emits no
`-debuginfo` subpackage, so a `RelWithDebInfo` package carries debug symbols
*inside* the setuid binary — measured on el9, 17 MB of binary in a 4.4 MB
package, against 3.1 MB in 955 KB for `Release`.

The package owns the same file list as Option B, with `/usr/bin/isolator` forced
to `%attr(4755,root,root)` and `/var/lib/isolator_boxes` owned as a directory so
`dnf remove` cleans it up. The binary statically links libstdc++/libgcc and
yaml-cpp, so it has **no dependency on the gcc-toolset SCL runtime** at execution
time — build under the toolset, run against the plain system.

An alias-built package owns `/usr/bin/isolate`, so RPM will refuse to install it
alongside the upstream `isolate` package (that file conflict *is* the
coexistence guard); remove upstream Isolate first. A default
(alias-less) package owns no such path and installs beside it.

There is no boot-time service or `--init`-style system setup step to enable.

### The `isolate` alias (`-DISOLATE_ALIAS`, default `OFF`)

The alias is what makes the drop-in path work: the ReCodEx Worker `execvp`s a
PATH-resolved binary literally named `isolate`, and its RPM declares
`Requires: isolate`. One CMake option covers all three of its parts:

| | `ISOLATE_ALIAS=OFF` (default) | `ISOLATE_ALIAS=ON` |
| --- | --- | --- |
| `<bindir>/isolate` symlink | not installed | installed |
| `man isolate` redirect page | not installed | installed |
| RPM `Provides: isolate` | not declared | declared |

It is **opt-in** because taking over `/usr/bin/isolate` displaces upstream
Isolate on the host. A default build is the neutral one: it installs only
`isolator`, coexists with upstream Isolate, and is all you need for standalone
`--yaml` mode.

Turn it on by adding `--alias` on any mode that configures:

```sh
scripts/isolator.sh install --alias
scripts/isolator.sh package --alias
```

Two consequences of the default. A default-built package cannot satisfy the
Worker's `Requires: isolate`, so a Worker deployment must be handed an alias
build. And flipping to `--no-alias` and re-installing does *not* remove an alias
an earlier install left behind — `uninstall` (which knows about the symlink) is
what cleans it up.

### Verifying an installation

```sh
ls -l /usr/bin/isolator                 # -rwsr-xr-x, owner root
man -w isolator                         # man page resolves

isolator --init --box-id=999            # as a non-root user: prints the box root
isolator --cleanup --box-id=999         # and tears it back down
```

The `--init` / `--cleanup` round-trip as an unprivileged user is the meaningful
check for Options B and C: it only succeeds if the setuid bit is in place. On an
alias build, repeat it as `isolate` — that `command -v isolate` resolves to our
symlink is the drop-in path's precondition.

---

## ⚡ Quickstart Guide

### 1. System Setup
No system initialization step is required: on its first `--run` the isolator
lazily creates the shared cgroup parent (`/sys/fs/cgroup/isolator_boxes`) and
enables its controllers. To tear the shared cgroup tree and box
directory (`/var/lib/isolator_boxes`) back down:
```sh
scripts/isolator.sh purge
```

### 2. Build the Isolator
```sh
scripts/isolator.sh build
```
(That is installation Option A above — see the Installation section for the
system-wide and RPM options.)

### 3. Run with Configuration
```sh
sudo ./build/src/isolator --yaml=/path/to/config.yml
```
### 4. Purge
To reclaim the host-wide state Isolator instances leave behind (the box tree and
the shared cgroup parent):
```sh
scripts/isolator.sh purge
```
---

## 🧪 Test suites

The repository has two tiers of test plus the ReCodEx integration suite:

### Unit tests (`tests/unit`)

Host-side [GoogleTest](https://github.com/google/googletest) tests of pure
logic — CLI/config parsing and the box lock — needing **no root**. Fetched via
CMake only when `-DTESTING=ON`, and run with `ctest`:

```sh
scripts/isolator.sh build --dev          # Debug + -DTESTING=ON
cd build && ctest --output-on-failure
```

### Workload tests (`tests/workload`)

[pytest](https://pytest.org) tests that drive the whole `isolator` binary
end-to-end, running **workloads** (small in-box payload programs under
`tests/workload/workloads/`) inside the sandbox to verify resource limits and
isolation boundaries actually bite. These need **root** (cgroups + namespaces):

```sh
scripts/isolator.sh build --dev   # builds the binary and the workloads
scripts/workload_tests.sh         # venv + sudo pytest
```

A default `build` compiles **no** test code, so `--dev` (or `--testing`) is what
puts the workloads in `tests/workload/build/`; without them the tier skips and
tells you which command to run.

`scripts/workload_tests.sh` is idempotent: on first run it creates a local
`.venv` (gitignored) from `tests/requirements.txt`, then runs pytest under
`sudo`. Pass pytest args through (`scripts/workload_tests.sh -k limits`, or
`--co` to collect without root). The suite skips itself cleanly when the binary
isn't built or root isn't available, so collection off a root host is a safe
no-op. (System Python on Arch/PEP-668 distros is externally managed, hence the
venv rather than a global `pip install`.)

### Mock evaluator (`tests/recodex`)

Replays real, production-harvested ReCodEx job configs (C, Python, C#, Maven) to
validate toolchains and limits. It drives the Isolator in **standalone mode**
(`--yaml=`), so it deliberately covers no part of the compatibility CLI as the
Worker actually emits it; that validation belongs to ReCodEx's own integration
pipeline, against an installed alias build.

⚠️ **Warning**: This test suite performs extensive setup and downloads!

Running these tests will:
- Clone the ReCodEx worker repository from GitHub
- Install Python dependencies (pandas)
- Install .NET runtime (requires sudo)
- Download approximately 1GB of test data from an external server
- Build additional components

These tests are primarily intended for integration with the ReCodEx evaluation system.

To run the ReCodEx tests:
```sh
cd tests/recodex
sudo ./recodex_init.py  # Setup (downloads ~1GB data)
./recodex_mock.py [language_groups]  # Run tests for specified language groups
```

Where language_groups is a subset of [C#, Python, C++, AdvC++]

### Variable Substitution

The test suites use variable substitution with the `${VARIABLE}` syntax to run without additional setup. For example, the test scripts use this feature to replace `${FILE_DIR}` with the absolute path to the test directory:

```yaml
box-fs:
  dir-rules:
    - "build=${FILE_DIR}/build"
    - "res=${FILE_DIR}/res:rw"
```

When creating manual configuration files, you should either replace these variables with absolute paths or implement similar substitution logic.


##  Configuration Reference

The isolator uses YAML configuration files to define sandbox environments and tasks.

```yaml
# Global settings and credentials
root-dir: "/var/lib/isolator_boxes"                  # Root directory for all sandboxes
root-cgroup: "/sys/fs/cgroup/isolator_boxes" # Root cgroup path
share-net: false                            # Whether to share network namespace with parent

credentials:
  id: "unique-instance-id"                  # Unique identifier for this instance
  name: "example-container"                 # Descriptive name for this container
  as-uid: 1000                              # User ID for running processes in the sandbox
  as-gid: 1000                              # Group ID for running processes in the sandbox

# Environment variables configuration
env:
  vars:                                     # Environment variables visible in the sandbox
    - "PATH=/usr/bin"                       # Set a specific variable
    - "HOME"                                # Inherit HOME from parent environment
    - "TEMP=/tmp"                           # Define a new variable
    - "LANG="                               # Remove variable from environment
  inherit-all: false                        # Whether to inherit environment from the host

# Filesystem configuration
box-fs:
  use-defaults: true                        # Mount standard directories (/bin, /lib, etc.)
  dir-rules:                                # Directories visible in the sandbox
    - "tests=/home/user/project/tests"      # Mount external directory
    - "tests=/home/user/project/tests:rw"   # Mount with read-write permissions
    - "tmp:tmp"                             # Create temporary directory
    - "proc:fs"                             # Mount /proc
    - "dev:dev"                             # Mount /dev with device access
    - "lib64:maybe"                         # Mount only if exists
    - "data:rw,noexec"                      # Multiple options (rw, noexec)
    - "custom=/path/to/dir:norec"           # Do not bind mount recursively

# Tasks to execute in the sandbox
tasks:
  - task-id: "example1"                     # Name for this task (used in output files)
    stats-yaml: "results1.yml"              # Path to output results file
    cmd:
      bin: "tests/example"                  # Path to the executable inside the sandbox
      args:                                 # Optional arguments for the executable
        - "Hello"
        - "World"
    stdin: "input.txt"                      # Redirect stdin from file (optional)
    stdout: "output.txt"                    # Redirect stdout to file (optional)
    stderr: "error.txt"                     # Redirect stderr to file (optional)
    stderr-to-stdout: false                 # Redirect stderr to stdout (optional)
    chdir: "tests"                          # Change directory before execution (optional)
    
    limits:                                 # Resource limits for this task
      mem: 5000000                          # Memory limit in bytes
      as-size: 10000000                     # Address space size limit in bytes
      stack: 8192                           # Stack size limit in KB
      cpu-time: 3                           # CPU time limit in seconds
      wall-time: 5                          # Wall clock time limit in seconds
      extra-time: 0.5                       # Grace period after CPU limit in seconds
      disk-usage: 1000000                   # Disk quota in blocks
      processes: 10                         # Maximum number of processes/threads
      open-files: 64                        # Maximum open file descriptors
      fsize: 1024                           # Maximum file size in KB
      core: 0                               # Maximum core dump size in KB

  - task-id: "example2"                     # A second task in the same sandbox
    cmd:
      bin: "/bin/echo"
      args:
        - "Another task"
    stdout: "output2.txt"
    limits:
      mem: 1000000
      cpu-time: 1
```

### Omitted vs. zero limits

Every limit is optional; **omit it to mean "no limit"**. A limit written as `0` is
never silently read as "unlimited":

- `mem`, `as-size`, `fsize`, `open-files`, `core` and `disk-usage` **enforce a
  literal `0`** — ask for zero and you get zero.
- `stack: 0` and `processes: 0` are **refused** as a usage error (exit code 2). A
  zero stack leaves the task unable to `execve` at all, and a process cap of `0`
  merely duplicates `1`, so both are mistakes rather than strict settings.
- An **omitted `stack`** is the one limit still applied, as *unlimited*, rather
  than inheriting the caller's (typically 8 MiB) stack — otherwise a deeply
  recursive task's verdict would depend on the shell that launched the isolator.

## 📁 Directory Rules and Sandboxed Paths

The isolator creates a secure sandbox environment with a strictly controlled filesystem. The `box-fs` section in the configuration defines how the filesystem should be structured within the sandbox.

### Directory Rules Syntax

Directory rules use the following syntax:
```yaml
box-fs:
  dir-rules:
    - "target=/host/path[:options]"
```

Where:
- `target` is the path inside the sandbox (relative to sandbox root)
- `/host/path` is the absolute path on the host system
- `options` are optional access modifiers described in the above example, separated by commas

For example:
```yaml
box-fs:
  dir-rules:
    - "bin=/bin"                        # Mount /bin as read-only
    - "tmp=/tmp/mytmp:rw, dev"          # Mount with read-write access and allow devices
```

### Important Note About Paths

**All paths specified within a task configuration are relative to the sandbox root, not the host filesystem.** This is crucial to understand when configuring:

- `chdir` - Working directory for the command (relative to sandbox root)
- `cmd.bin` - Path to the executable (relative to sandbox root)
- `stats-yaml` - Output file for statistics (relative to sandbox root)
- `stdin/stdout/stderr` - I/O file paths (relative to sandbox root)

For example, if you have the following directory rule:
```yaml
box-fs:
  dir-rules:
    - "build=/home/user/myproject/build"
    - "res=/home/user/myproject/results:rw"
```

Then your task configuration would reference these paths as:
```yaml
tasks:
  - task-id: "example-task"
    chdir: "/res"                     # Inside the sandbox at /res
    stats-yaml: "/res/stats.yaml"     # Save results to /res/stats.yaml
    cmd:
      bin: "/build/myprogram"         # Run /build/myprogram
      args: ["input.txt"]
```

## 📊 Metadata file

After execution, the isolator generates metadata in YAML format with information about the run — this is the `stats-yaml` of **standalone mode** (`--yaml=`):

```yaml
status: OK                   # Status: OK, killed, memory, wall-time, cpu-time
exitcode: 0                  # Process exit code
exitsig: 0                   # Signal that terminated the process (if any)
time: 0.125                  # CPU time used (seconds)
memory: 8520                 # Memory usage (KB)
wall-time: 0.135             # Wall clock time (seconds)
```

Possible status values:
- `OK`: Task completed successfully
- `killed`: Task was terminated by a signal
- `non zero exit code`: Task exited with non-zero code
- `wall-time`: Wall time limit exceeded
- `cpu-time`: CPU time limit exceeded
- `memory`: Memory limit exceeded

**Compatibility mode writes a different file.** Driven as a drop-in replacement
for Isolate (`--init` / `--run` / `--cleanup` with `--meta=FILE`), the isolator
emits Isolate's `key:value` **meta-file** instead, as a superset of what Isolate
writes so the ReCodEx Worker parses it unchanged:

| Key | When |
| --- | --- |
| `status` | only on failure — `RE` / `SG` / `TO` / `XX`; absent means success, as in Isolate |
| `exitcode` | the task exited normally |
| `exitsig` | the task died on a signal |
| `killed:1` | *we* `SIGKILL`ed it — the wall/CPU-timeout path |
| `time`, `time-wall`, `max-rss`, `csw-voluntary`, `csw-forced` | always |
| `cg-mem` | cgroup memory was measurable (omitted otherwise, leaving `max-rss` as the memory signal) |
| `cg-oom-killed:1` | the kernel OOM-killed something in the box |

The YAML above is never written on that path, and conversely `--meta` has no
effect on the standalone path — the two output formats belong to the two modes.

## 🔍 Troubleshooting

- Running the isolator itself requires root — unless it is installed setuid, which is the point of Options B and C. Of the helper modes, `build` and `package` need no privilege at all; `install`, `uninstall` and `purge` elevate the single command that needs it (via `sudo`) rather than running wholesale as root, so your build tree never ends up root-owned.
- If tests fail with filesystem errors, ensure that the directories specified in the configuration exist and have appropriate permissions.
- The isolator self-arranges its cgroup parent on first `--run`; if the shared cgroup tree or box directory gets into a bad state, reset it with `scripts/isolator.sh purge`.
- Always use absolute paths in host filesystem references but remember that paths inside the task configuration are relative to the sandbox root.
- When testing, inspect the content of `/var/lib/isolator_boxes/` to see the actual sandbox structure.
- Run the isolator binary with --debug to see detailed logs.

---
