# CONTEXT

Domain language and key decisions for the **ReCoDex Isolator** (`rcdx_cntnr`) — a lightweight Linux containerization tool written from scratch in C++23 to sandbox untrusted code for the [ReCodEx](https://github.com/ReCodEx) automated assignment evaluation system.

This file is the glossary skills should use when naming concepts. The deep rationale lives in `docs/thesis.pdf`; architectural decisions are summarized under [Decisions](#decisions) and should graduate to `docs/adr/` as they're revisited.

---

## Glossary

### Tool concepts

- **Isolator** — this tool as a whole. A modern, ReCodEx-specific replacement for **Isolate** (the Mareš/Blackham IOI sandbox that ReCodEx currently uses).
- **Compatibility mode** (drop-in) — running the Isolator under Isolate's CLI: three independent phases (`--init` / `--run -- prog args` / `--cleanup`) keyed by a caller-supplied `--box-id`, emitting an Isolate-format **meta-file** and `0/1/other` exit codes. State persists between phases on-disk only (the box directory). This is how the ReCodEx Worker drives it. See `docs/adr/0001`.
- **Standalone mode** — running the Isolator under its own single-shot CLI (`--yaml=cfg.yml`): one instance parses one config, runs all its tasks sequentially, and emits per-task **stats-yaml**. Used by the test suites.
- **Instance** — one run of the Isolator: parse config → reserve resources → run tasks → emit metadata. Instances are designed to run concurrently without interfering.
- **Instance ID** (`box_id`) — a unique integer per instance. In compatibility mode it is caller-supplied (`--box-id`, unique per Worker thread); in standalone mode it is configured, else generated. The instance's filesystem root, cgroup path, and UID/GID are all *derived* from it. See `docs/adr/0003`.
- **Sandbox** / **box** — the restricted execution environment (namespaces + cgroups + UID/GID + mounts + filesystem view). "box" is the identifier prefix (`box_uid`, `box_gid`, `box_root`, `box_cgroup`); prose prefers "sandbox". Treat them as synonyms.
- **Box directory** (`/box`) — the single writable working directory inside the sandbox, where the Worker stages job files and the task reads/writes. On the host it is `<box_root>/box`; inside the sandbox it is `/box`. Created at `--init` owned by `box_uid:box_gid` (currently mode `0777` — see the ReCodEx integration constraints). Distinct from **box_root** (the sandbox filesystem root) and from the "box" identifier prefix above: those name the *whole* sandbox, this names *one directory* within it. Isolate's `--init` prints `box_root` to stdout; the Worker appends `/box`.
- **Task** — a single unit of execution inside the sandbox: one executable with args, I/O redirection, and resource limits. An instance runs one or more tasks sequentially.

### The three processes

An instance is three processes with distinct privilege/responsibility (see [Decisions](#decisions)):

- **Root process** (`root_core`) — reserves *non-isolated* global resources (instance ID, UID/GID, instance cgroup, instance root dir), launches the proxy, then supervises and cleans up. Runs as the launching (root) user.
- **Proxy process** (`proxy_core`) — launched via `clone3()` into the new namespaces/cgroups. Builds the in-sandbox view (mount namespace, filesystem tree, cgroup subtree), then launches and oversees task processes. Becomes PID 1 in the PID namespace.
- **Task process** (`task_supervisor` → `execve`) — runs the untrusted program under all restrictions.

### Configuration vocabulary

- **Directory rule** — a `box-fs.dir-rules` entry mapping a host path (or virtual fs) into a sandbox location with options: read-only (default) / `rw`, `maybe` (mount only if it exists), `fs` (mount a virtual filesystem like procfs), `tmp` (create a temp dir), `dev` (allow devices), `noexec`, `norec` (non-recursive bind). **All task paths (`bin`, `chdir`, `stdin/out/err`, `stats-yaml`) are relative to the sandbox root, not the host.**
- **Resource limits** (`resource_limits`) — per-task caps: `mem`, `as-size`, `stack`, `cpu-time` (+`extra-time` grace), `wall-time`, `disk-usage`, `processes`, `open-files`, `fsize`, `core`. Enforced via cgroups (mem/cpu/pids) or rlimits (the rest) — see [Decisions](#decisions).
- **Metadata / stats** — per-task result: `status` (one of `exit_status`: `OK`, `WALL_TIME_EXCEEDED`, `CPU_TIME_EXCEEDED`, `MEMORY_LIMIT_EXCEEDED`, `KILLED`, `NON_ZERO_EXIT_CODE`), exit code/signal, CPU time, memory, wall time. Sourced from **both** cgroup accounting and `getrusage()`. Emitted two ways: **stats-yaml** (`task_stats`, our YAML format) in standalone mode; **meta-file** (Isolate's `key:value` text format — `status: RE/SG/TO/XX`, `exitcode`, `exitsig`, `killed`, `time`, `time-wall`, `cg-mem`, `max-rss`, `csw-voluntary`, `csw-forced`, …) in compatibility mode, written as a *superset* so future ReCodEx can consume extra metrics. **Superset** here means every key Isolate emits, always present, *plus* extra keys under distinct names (the Worker ignores unknown keys) — not a change to Isolate's per-key presence rules. `time` and `cg-mem` are cgroup-sourced, `max-rss` and `csw-*` rusage-sourced; memory in **binary KB** (bytes ÷ 1024). **`cg-mem`** (cgroup subtree peak) and **`max-rss`** (rusage per-process peak) are distinct signals; `cg-mem` is the one key *omitted* when the kernel can't measure it, leaving `max-rss` as the memory signal (see `docs/adr/0006`).

### Linux mechanisms (as used here)

- **Namespaces** — created via `clone3()` flags. Used: **mount** (`CLONE_NEWNS`), **PID** (`CLONE_NEWPID`, proxy is PID 1), **cgroup** (`CLONE_NEWCGROUP` + `CLONE_INTO_CGROUP`), **UTS**, **IPC**, and **network** (`CLONE_NEWNET`, loopback-only — skipped when `share-net` is set). **User namespaces are deliberately *not* used.**
- **cgroups v2** (`cgroupv2_t`) — the single unified hierarchy at `/sys/fs/cgroup`, driven by creating directories and writing controller files. Enabled **controllers**: `cpu`, `memory`, `pids` (via `+name` → `cgroup.subtree_control`). Key files: `cgroup.procs`, `cpu.max`/`cpu.stat`, `memory.max`/`memory.min`/`memory.peak`, `pids.max`.
- **leaf cgroup** / **proxy_leaf** — empty sub-cgroups the root/proxy relocate themselves into before enabling controllers (controllers can't be enabled in a populated cgroup).
- **rlimits** — per-process `setrlimit()` caps (`RLIMIT_AS`, stack, open files, file size, core, CPU time), inherited across `fork()`. Used for limits cgroups can't express per-process.
- **Credentials** — real/effective/saved UID & GID. Each box gets a dedicated `box_uid`/`box_gid` (= `instance_id + 60000`) for file ownership and privilege drop. `proxy_credentials_manager` switches via `setresuid`/`setresgid`/`setgroups` (`switch_to_box` / `switch_to_user`); order matters.
- **Filesystem assembly** — `pivot_root(".", ".")` then `umount2(".", MNT_DETACH)` to swap root (the alternative `chroot` is only mentioned, not used); **bind mounts** (`MS_BIND|MS_REC`) for directory rules and to bind the box root onto itself (pivot_root needs a mount point); **mount propagation** set `rslave` (`MS_SLAVE|MS_REC`) so host→sandbox events propagate but not back.
- **Disk quota** (`quotactl`) — disk-usage limits via `QUOTACTL(2)`; requires a quota-capable filesystem (e.g. ext4). `devices.hpp` resolves the backing device for a path. *(Implemented in code per the README; not covered by the thesis.)*
- **Syscalls of note** — `clone3()` (spawn into namespaces+cgroup atomically), `execve()`, `waitpid()` (root waits on proxy; proxy polls tasks with `WNOHANG`), `getrusage()`.

### Testing

- **Unit test** — a host-side test of pure logic (CLI/config parsing, the box lock) that needs no root; sad paths are process-exit assertions (death tests), since errors go through `terminate()`/`usage_error()` → `exit(2)`, not exceptions (ADR 0005).
- **Workload test** — a test that drives a whole **instance** end-to-end through the binary (needs root + cgroups), covering both the standalone YAML flow and the three-phase compatibility lifecycle.
- **Workload** — an in-box payload program a workload test runs *inside the sandbox* to verify a resource limit or isolation boundary actually bites (e.g. a program that allocates past the memory cap). Distinct from a **task** (the domain unit of execution): a workload is a task's executable chosen specifically to probe an enforcement boundary.

---

## Decisions

Summarized from `docs/thesis.pdf`. Promote to `docs/adr/NNNN-*.md` when reopened.

- **Custom tool over Docker/Podman** — mainstream runtimes aren't lightweight or precise enough for ReCodEx's resource measurement.
- **Replace Isolate specifically** — Isolate targets the IOI, not ReCodEx; this tool is engineered for ReCodEx's needs.
- **cgroups v2, not v1** — unified, modern resource-management interface.
- **Three-process model (root/proxy/task)** — separation of concerns: privileged global setup + cleanup happens *outside* isolation (root); in-sandbox setup needs to see the whole host fs (proxy); untrusted code runs fully restricted (task).
- **No user namespaces; allocate a dedicated UID/GID per instance instead** — the design assumes root throughout and isolates credentials by deriving `box_uid`/`box_gid` (`instance_id + 60000`) and dropping to them.
- **Derive all per-instance resources from one instance ID** — fs path, cgroup path, and UID/GID are computed from it to avoid collisions. In standalone mode where no ID is supplied, uniqueness is guaranteed by an **atomic `mkdir` claim + retry** (hash of name+time only generates candidates); the allocator-daemon idea is rejected — see `docs/adr/0003`.
- **Isolate-compatible drop-in mode** over the single-shot core, and **setuid-root install with an `isolate` alias** so the non-root Worker can invoke it unchanged — see `docs/adr/0001`, `docs/adr/0002`. Target platform Rocky 9 / RPM — see `docs/adr/0004`.
- **`clone3()` + `CLONE_INTO_CGROUP`** — atomically place the child into its target cgroup at creation; with `CLONE_NEWCGROUP` that cgroup becomes the namespace root.
- **Relocate to leaf cgroups before enabling controllers** — controllers can't be enabled in a populated cgroup.
- **Enforce memory with `memory.max` + `memory.min=max` + swap OFF** — with swap on, the kernel reclaimed memory and let the process exceed `memory.max`; `memory.min=max` alone didn't help. Only disabling swap gave consistent termination at the limit. *(Documented kernel pitfall.)*
- **cgroups for mem/cpu/pids, rlimits for the rest** — rlimits can't measure cumulative usage across a process hierarchy.
- **Measure wall time externally in the proxy** — no kernel-enforced wall-clock limit exists; the proxy polls `waitpid(WNOHANG)`, sleeps to avoid busy-waiting, and `SIGKILL`s on overrun.
- **Combine cgroup + `getrusage` stats** — cgroup gives subtree-wide totals (catches multi-process/threaded use); rusage adds peak RSS and user/system CPU.
- **Build the sandbox fs via mount namespace + bind mounts + pivot_root**, with a fresh `cgroup2` remounted inside so task handlers see only the sandbox's cgroups.
- **Mount propagation = slave-recursive** — blocks sandbox→host mount events, narrowing a known vulnerability surface.
- **Network: full isolation (loopback-only) by default, or full host access via `share-net`** — only two modes today; restricted inter-sandbox networking (veth) is future work.

---

## ReCodEx integration constraints

- **Drop-in for ReCodEx's process-isolation subsystem** — must replace and extend `Isolate`. Real Worker integration is still pending (both sides need CLI/output-format adjustment).
- **Worker runs jobs as a task DAG** — compile → run → evaluate ("Judges") with dependencies; some tasks are `fatal-failure`. The "instance = sequential related tasks" model mirrors this.
- **Shared writable dirs across job phases** — phases run as *separate* Isolator invocations sharing files, which forced writable sandbox dirs to mode `0777`. **Explicitly insecure — the top security item to fix.**
- **Offline, diverse toolchains** — validated on C++, Python, C# (.NET). Pitfalls baked into config behavior: GCC needs the host `PATH` inherited and a `LD_LIBRARY_PATH` derived from `gcc -print-search-dirs`; C# uses a custom offline `/opt/dotnet` pointing at `Roslyn/csc.dll`.
- **Three test tiers** — **unit tests** (host-side, no root) cover pure logic: CLI/config parsing and the box lock; **workload tests** drive a whole instance end-to-end through the binary (need root/cgroups), running **workloads** (in-box payload programs) to verify a limit or isolation boundary actually bites; the **mock Worker** (`tests/recodex`) simulates ReCodEx over real job configs. See the [Testing](#testing) glossary.

---

## Conventions

- C++23, header-heavy (`src/*.hpp`), built with CMake (`scripts/isolator_build.sh`); logging via a thin `logs::` wrapper over `std::format`, writing to **stderr** (stdout is reserved for `--init`'s box-root path) — see `docs/adr/0002`.
- Class names are snake_case: `root_core`, `proxy_core`, `proxy_connector`, `proxy_mount_manager`, `dir_rule_supervisor`, `task_supervisor`, `cgroupv2_t`, `*_credentials_manager`.
- Fatal errors go through `terminate(...)` (log + `exit(2)`, the Isolator's internal-error code — ADR 0005).
- Requires root; needs a cgroupv2-enabled kernel. Initialize with `scripts/isolator_init.sh`, clean up with `scripts/isolator_cleanup.sh`.

When you name a domain concept in an issue, refactor, or test, use the term as defined above. If a concept isn't here yet, that's a signal — either reconsider the wording or note the gap (e.g. via `/grill-with-docs`).
