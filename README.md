# ReCoDex Isolator 🚀

Lightweight Linux containerization tool written "from scratch", primarily intended for evaluation of programming assignments.

---

### 📚 Reading this README
This project uses advanced Linux concepts (namespaces, cgroups, UID/GID mappings, etc.). For a deeper dive, see my thesis (link placeholder).

Terminology used throughout this README and the codebase:

#### 🧠 Terminology

- **Isolator** — This tool as a whole.
- **Instance** — One run of the isolator, from parsing the configuration file to creating a file with metadata about the run.
- **Sandbox** — The isolated environment that is created based on the configuration file. Includes namespaces, cgroups, UID/GID, environment variables, ...
- **Task** — A single unit of execution of the isolator, runs an executable with specified arguments in a sandbox.

#### 🧵 Processes
An instance runs three different processes:
- **Root process** — Reserves necessary global resources.
- **Proxy process** — Prepares a sandbox.
- **Task process** — Runs the isolated executable itself.

---

### 🛠️ System prerequisites

- You need a kernel with cgroupv2 enabled. Check with:
  ```sh
  mount | grep cgroup
  ```
- CMake 3.20 and a compiler capable of C++23.
- Boost `program_options` package.
- For using limits on disk usage, the container itself has to run on a filesystem that supports `QUOTACTL(2)` (e.g. ext4).

---

### ⚡ Quickstart guide

After installing everything required, run:
```sh
scripts/initialize_system.sh
```
It shouldn't be necessary to run it again after reboot.

Build with:
```sh
scripts/build.sh
```

Run the container with:
```sh
scripts/run.sh --yaml=<path_to_yaml_configuration_file>
```
The script cleans up and recompiles before running again.

---

### 📝 Example configuration file

```yaml
env:
  dir-rules:                                 # optional user specified list of directory rules declaring the directories that will exist
                                            # inside the sandbox, the syntax is described [here]()
    - "tests=/home/user/project/tests"
  use-defaults: false                        # mount a default list of directories into the sandbox (like /lib, /lib64, /bin, ...), true is the default if not specified 

tasks:
  - task-id: "example"                    # (mandatory) name of the task (has to be unique within one run of the container), is used in output files of the sandbox
    path: "tests/example"                 # (mandatory) path to the executable inside of the sandbox - you have to use a directory rule to get it there.

    args:                                   # Optional list of arguments passed to the executable in an execve call.
      - "Hello"
      - "World" 

    rlims:                                  # Optional node with specification of resource limits for the task. Times are in seconds and memory sizes in bytes.
      mem: 5000000                          # If any of these is not specified, no limit will be set. The exception is a default wall time limit of 20s.
      cpu-time: 3
      wall-time: 5
      disk-usage: 1000000
```

---

## ⚙️ Configuration overview

The configuration file comprises of YAML nodes organised hierarchically into smaller units.

### 🌿 Environment
Contains configuration of the environment variables that will be visible in the sandbox environment. It comprises of a list of environment rules:

- `'var'`:
  Inherit the variable `var` from the parent.

- `'var=value'`:
  Set the variable `var` to `value`. When the `value` is empty, the variable is removed from the environment.

- `full-env`:
  Inherit all variables from the parent.

### 📁 Sandbox-fs
Contains configuration of the directory tree visible in the sandbox. It contains only subtrees
requested by directory rules in a list:

- `'in'='out'[:'options']`:
  Bind the directory `out` as seen by the caller to the path `in` inside the sandbox.
  If there already was a directory rule for `in`, it is replaced.

- `'dir'[:'options']`:
  Bind the directory `/dir` to `dir` inside the sandbox.
  If there already was a directory rule for `in`, it is replaced.

By default, all directories are mounted read-only and restricted (no devices,
no setuid binaries). This behavior can be modified using the 'options':

* `rw` — Allow read-write access.

* `dev` — Allow access to character and block devices.

* `noexec` — Disallow execution of binaries.

* `maybe` — Silently ignore the rule if the directory to be bound does not exist.

* `fs` — Instead of binding a directory, mount a device-less filesystem called `in`. For example, this can be `proc` or `sysfs`.

* `tmp` — Bind a freshly created temporary directory writable for the sandbox user. Accepts no `out`, implies `rw`.

* `norec` — Do not bind recursively. Without this option, mount points in the outside directory tree are automatically propagated to the sandbox.



Unless *--no-default-dirs* is specified, the default set of directory rules binds +/bin+,
+/dev+ (with devices allowed), +/lib+, +/lib64+ (if it exists), and +/usr+. It also mounts the proc filesystem at +/proc+.

### 🚦 Task
Contains configuration of a task.

- `stdin: 'file'`:
  Redirect standard input from `'file'`. The `'file'` is a path relative to the root of the sandbox. 
  If not specified, standard input is inherited from the launch of this instance.

- `stdout: 'file'`:
  Redirect standard output to `'file'`. The `'file'` has to be accessible
  inside the sandbox (which means that the sandboxed program can manipulate
  it arbitrarily). If not specified, standard output is inherited from the launch of this instance, and the sandbox manager does not write anything to it.

- `stderr: 'file'`:
  Redirect standard error output to `'file'`. The `'file'` has to be accessible
  inside the sandbox (which means that the sandboxed program can manipulate
  it arbitrarily). If not specified, standard error output is inherited from the
  parent process. See also `--stderr-to-stdout`.

- `stderr-to-stdout: 'true'\'false'`:
  Redirect standard error output to standard output. This is performed after
  the standard output is redirected by `--stdout`. Mutually exclusive with `--stderr`.

- `chdir: 'dir'`:
  Change directory to `'dir'` before executing the program. This path must be
  relative to the root of the sandbox.

### 📊 Resource limits

- `cpu-time: 'time'`:
  Limit run time of the program to `'time'` seconds. Fractional numbers are allowed.
  Time in which the OS assigns the processor to other tasks is not counted.
  If this limit is exceeded, the program is killed (after `--extra-time`, if set).

- `wall-time: 'time'`:
  Limit wall-clock time to `'time'` seconds. Fractional values are allowed.
  This clock measures the time from the start of the task to its exit,
  so it does not stop when the program has lost the CPU or when it is waiting
  for an external event. We recommend to use `--time` as the main limit,
  but set `--wall-time` to a much higher value as a precaution against
  sleeping programs.
  If this limit is exceeded, the program is killed.

- `extra-time: 'time'`:
  When the `--time` limit is exceeded, do not kill the program immediately,
  but wait until `--extra-time` seconds elapse since the start of the program.
  This allows to report the real execution time, even if it exceeds the limit
  slightly.

- `memory: 'bytes'`:
  Limit total utilization of memory as measured by cgroups accounting to `'bytes'`.
  If the limit is exceeded, the task is killed.

- `disk-usage: 'blocks'`:
  Set disk quota to a given number of blocks and inodes. This requires the
  filesystem to be mounted with support for quotas. Please note that this
  currently works only on the ext family of filesystems (other filesystems
  use other interfaces for setting quotas).
  If the quota is reached, system calls expanding files fail with error `EDQUOT`.

- `processes: 'max'`:
  Permit the program to create up to `'max'` processes and/or threads.
  If this limit is exceeded, system calls creating processes fail with error
  `EAGAIN`.

