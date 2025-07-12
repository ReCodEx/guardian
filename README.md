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
scripts/isolator_init.sh
```
This script creates the necessary cgroups and directories common for all instances.

### 2. Build the Isolator
```sh
scripts/isolator_build.sh
```

### 3. Run with Configuration
```sh
scripts/isolator_run.sh <path_to_yaml_configuration_file>
```
or
```sh
scripts/isolator_run.sh --yaml=<path_to_yaml_configuration_file>
```
The script runs the isolator with your configuration.

### 4. Cleanup
To clean up resources used by the isolator:
```sh
scripts/isolator_cleanup.sh
```

---

## 🧪 Test suites

The repository includes two test suites:

### Basic Test Suite (`tests/test_suite`)

A lightweight test suite that verifies core functionality:

- **Isolation Tests**: Verify namespace isolation features
- **Resource Limits Tests**: Check that resource limits are properly enforced

To run the basic tests:
```sh
cd tests/test_suite
python3 run_tests.py
```

This test suite is quick to run and doesn't require extensive setup.

### ReCodEx Integration Tests (`tests/recodex`)

⚠️ **WARNING**: This test suite requires extensive setup and downloads!

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
python3 recodex_init.py  # Setup (downloads ~1GB data)
python3 recodex_mock.py  # Run tests
```

Only run these tests if you need to verify ReCodEx integration and have sufficient bandwidth and storage available.

## 📝 Configuration Reference

The isolator uses YAML configuration files to define sandbox environments and tasks.

### Example Configuration:

```yaml
credentials:
  id: "unique-instance-id"   # Optional unique identifier for this instance

env:
  vars:                     # Environment variables visible in the sandbox
    - "PATH=/usr/bin"       # Set a specific variable
    - "HOME"                # Inherit HOME from parent environment
    - "full-env=false"      # Whether to inherit all environment variables
  inherit-all: false        # Alternative way to inherit all variables

box-fs:
  dir-rules:                # Directories visible in the sandbox
    - "tests=/home/user/project/tests"    # Mount external directory
    - "tmp:tmp"                          # Create temporary directory
    - "proc=proc:fs"                     # Mount proc filesystem
  use-defaults: true        # Mount standard directories (/bin, /lib, etc.)

tasks:
  - task-id: "example"      # Name for this task (used in output files)
    cmd:
      bin: "tests/example"  # Path to the executable inside the sandbox
      args:                 # Optional arguments for the executable
        - "Hello"
        - "World"
    stdin: "input.txt"      # Redirect stdin from file (optional)
    stdout: "output.txt"    # Redirect stdout to file (optional)
    stderr: "error.txt"     # Redirect stderr to file (optional)
    stderr-to-stdout: false # Redirect stderr to stdout (optional)
    chdir: "tests"          # Change directory before execution (optional)
    
    limits:                 # Resource limits for this task
      mem: 5000000          # Memory limit (bytes)
      cpu-time: 3           # CPU time limit (seconds)
      wall-time: 5          # Wall clock time limit (seconds)
      extra-time: 0.5       # Grace period after CPU limit (seconds)
      disk-usage: 1000000   # Disk quota (blocks)
      processes: 10         # Maximum number of processes/threads
      open-files: 64        # Maximum open file descriptors
      fsize: 1024           # Maximum file size (KB)
      core: 0               # Maximum core dump size (KB)
```

---

## ⚙️ Configuration Details

### Credentials and Global Settings

- `id`: Unique identifier for this sandbox instance
- `root-dir`: Root directory for all sandboxes (default: `/isolate_boxes`)
- `root-cgroup`: Root cgroup path (default: `/sys/fs/cgroup/isolate_boxes`)
- `as-uid`/`as-gid`: User/group ID for running processes in the sandbox
- `share-net`: Whether to share the network namespace with the parent process

### Environment Variables

Rules for environment variables:

- `'var'`: Inherit the variable `var` from the parent
- `'var=value'`: Set the variable `var` to `value`
- `'var='`: Remove the variable from the environment
- `'full-env=true'`: Inherit all variables from the parent

### Sandbox Filesystem

Directory rules format:

- `'in'='out'[:'options']`: Bind the directory `out` to path `in` inside the sandbox
- `'dir'[:'options']`: Bind the directory `/dir` to `dir` inside the sandbox

Available options:

- `rw`: Allow read-write access (default is read-only)
- `dev`: Allow access to character and block devices
- `noexec`: Disallow execution of binaries
- `maybe`: Silently ignore if source directory doesn't exist
- `fs`: Mount a filesystem (e.g., `proc`, `sysfs`) instead of binding a directory
- `tmp`: Create a fresh temporary directory (implies `rw`)
- `norec`: Do not bind recursively (don't propagate mount points)

Default directories mounted (when `use-defaults: true`):
- `/bin`, `/lib`, `/lib64` (if exists), `/usr`
- `/dev` (with devices allowed)
- `/proc` filesystem

### Tasks Configuration

Task execution settings:

- `stdin`/`stdout`/`stderr`: Redirect standard streams to files
- `stderr-to-stdout`: Redirect stderr to stdout
- `chdir`: Change working directory before execution

### Resource Limits

- `cpu-time`: CPU time limit in seconds (process execution time)
- `wall-time`: Wall clock time limit in seconds (real-world time)
- `extra-time`: Grace period after CPU time limit is exceeded
- `memory`: Memory usage limit in bytes
- `stack`: Stack size limit in kilobytes
- `processes`: Maximum number of processes/threads
- `open-files`: Maximum number of open file descriptors
- `fsize`: Maximum file size in kilobytes
- `core`: Maximum core dump size in kilobytes
- `disk-usage`: Disk quota in blocks (requires filesystem quota support)

---

## 📊 Output Information

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

---