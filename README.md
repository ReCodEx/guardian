# ReCoDex Isolator

Lightweight Linux containerization tool written from scratch for the ReCoDex assignment evaluation system. 

---

## 📚 Overview

This project uses advanced Linux kernel features (namespaces, cgroups, UID/GID mappings, etc.) to create secure sandboxes for running untrusted code. It provides fine-grained control over system resources and filesystem access. To get more insight into the details, you can take a look at my thesis.(TODO)

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
- CMake 3.20+ and a compiler supporting C++23
- Boost `program_options` library
- For disk usage quotas, the sandbox must be on a filesystem supporting `QUOTACTL(2)` (e.g., ext4)

---

## ⚡ Quickstart Guide

### 1. System Setup
First, initialize the required system resources:
```sh
sudo scripts/isolator_cleanup.sh
sudo scripts/isolator_init.sh
```
This script creates the necessary cgroups and directories common for all instances.

### 2. Build the Isolator
```sh
scripts/isolator_build.sh
```

### 3. Run with Configuration
```sh
sudo ./build/src/isolator --yaml=/path/to/config.yml
```
### 4. Cleanup
To clean up resources used by Isolator instances:
```sh
sudo scripts/isolator_cleanup.sh
```
---

## 🧪 Test suites

The repository includes two test suites, they serve as good examples of how the tool can be used:

### Basic Test Suite (`tests/test_suite`)

A lightweight test suite that verifies core functionality:

- **Isolation Tests**: Verify namespace isolation features
- **Resource Limits Tests**: Check that resource limits are properly enforced

To run the basic tests:
```sh
cd tests/test_suite
./run_tests.py
```

This script will:
- Build the necessary binaries
- Clean up previous test artifacts
- Initialize the isolation environment
- Run both isolation and resource limits tests
- Report detailed test results

This test suite is quick to run and doesn't require extensive setup.

### ReCodEx Integration Tests (`tests/recodex`)

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
root-dir: "/isolate_boxes"                  # Root directory for all sandboxes
root-cgroup: "/sys/fs/cgroup/isolate_boxes" # Root cgroup path
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

After execution, the isolator generates metadata in YAML format with information about the run:

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

## 🔍 Troubleshooting

- Running the tool and most of the helper scripts requires root privileges.
- If tests fail with filesystem errors, ensure that the directories specified in the configuration exist and have appropriate permissions.
- Check that the isolation environment has been properly initialized with `isolator_cleanup.sh` and `isolator_init.sh`.
- Always use absolute paths in host filesystem references but remember that paths inside the task configuration are relative to the sandbox root.
- When testing, inspect the content of `/isolate_boxes/` to see the actual sandbox structure.
- Run the isolator binary with --debug to see detailed logs.

---