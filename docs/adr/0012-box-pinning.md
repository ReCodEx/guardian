---
status: accepted
---

# Box pinning: caller-supplied, box-wide, exact

ReCodEx needs to restrict each box to a set of CPU cores and NUMA memory nodes
so that concurrent boxes on one worker don't disturb each other's measurements.
Isolate provides this through its config file: per-box `boxN.cpus` /
`boxN.mems` strings, written unchanged into `cpuset.cpus` / `cpuset.mems` of the
box cgroup (`isolate/cg.c:243-246`). The write happens in `cg_enter()`, which
the *task* process calls on itself (`isolate/isolate.c:934`), so only the
sandboxed program is pinned. The keeper and the proxy stay in the caller's
cgroup.

We implement **pinning** differently in four ways:

- **The caller supplies it on the command line**, as `--cpuset-cpus=LIST` and
  `--cpuset-mems=LIST` on every `--run`. There is no config file (we have none,
  see ADR 0007), and the **Worker owns the core map**: which box gets which
  cores, including whether sets overlap, is its decision, never checked by the
  Guardian.
- **The whole box is pinned**: root supervisor, proxy and every task. The sets
  are written on the instance cgroup before the root process moves into its
  `leaf`, and everything below inherits them from the nearest cpuset-enabled
  ancestor.
- **Pinning is exact.** A list must match ids-and-ranges (`N`, `N-M`, comma
  separated) at parse time. The kernel rejects ids that don't exist when we
  write it. After the write we require `cpuset.cpus` to equal
  `cpuset.cpus.effective` (and likewise for `mems`). Otherwise the `--run` fails
  with exit 2 and `status:XX`.
- **The flags are named after the kernel files they feed**, not after Isolate's
  config keys.

The `cpuset` controller is turned on for every `--run`, next to
`cpu`/`memory`/`pids`, whether or not the box is pinned.

## Considered options

- **An Isolate-style per-box config file.** Rejected. We ship no config file at
  all (ADR 0007), and the Worker already decides everything else about a box on
  the command line. A host file would split one box's definition between two
  owners: the admin for its cores, the Worker for everything else.
- **Pin only the task, as Isolate does.** Rejected. The supervisor does real work
  around the task: mounts, `pivot_root`, the ownership-dance `lchown_tree`, stat
  collection. Unpinned, that work lands on whatever core is free, which on a
  loaded worker is often a core reserved for another box. Pinning the
  supervisor costs nothing while the task runs: root blocks in `waitpid` and the
  proxy in `ppoll` on the task's pidfd, with no periodic wakeups. When the proxy
  does wake (to kill on wall time), `proxy_leaf` and the task cgroup are
  sibling `cpu` groups, so a task with many threads cannot starve it. Isolate's
  keeper, by contrast, wakes every 100 ms.
- **Trust the kernel's write.** Rejected. In cgroup v2 a write succeeds for any
  *possible* core or node, including offline ones (`cpuset_bind()` sets the top
  cpuset's allowed mask to `cpu_possible_mask`). When none of the requested ones
  can be granted, the effective set silently becomes the parent's
  (`compute_effective_cpumask()`). On a host with SMT turned off, as our own
  overhead-measurement setup does (`perf/harness/host-setup.sh` on the
  `perf/sandboxing-overhead-measurement` branch), `--cpuset-cpus=3` would run
  the box on every online core with no error.
- **Validate nothing at parse time and leave the syntax to the kernel.** Viable,
  since the kernel rejects malformed lists. Rejected because the accepted format
  would then vary with the kernel version (newer kernels also take strides,
  `N` and `all`), an empty list would silently mean "unpinned", and the check
  could not be unit-tested without root.
- **Turn cpuset on only when a pinning flag is given.** Rejected in favour of
  treating all four controllers the same. It would only spare hosts that never
  pin, and after the first pinned run the setting stays on until `purge` anyway.
- **`--cpus` / `--mems`** (Isolate's key names). Rejected: Docker's `--cpus=4`
  means four CPUs' worth of *time*, not core #4, and `--mems` is one letter from
  `--mem`. **`--cg-cpus` / `--cg-mems`** was rejected too, because `--cg-mems`
  sits next to the `--cg-mem` the Worker already sends.

## Consequences

- **cpuset is now required for every `--run`.** A host without the controller
  fails all runs, pinned or not. el9 runs cgroup v2 by default, and that
  includes cpuset.
- **CPU affinity set with `taskset` may be lost.** A process entering a cgroup
  with its own cpuset has its affinity reset to that cgroup's set. Kernels from
  6.2 on (`da019032819a`) intersect the reset with a mask set via
  `taskset`/`sched_setaffinity`. Older kernels discard the mask, and on them the
  first `+cpuset` write at the cgroup root resets the affinity of every process
  on the host. Whether el9's 5.14 has the backport needs checking on the target.
  Production is no worse than today: Isolate's keeper runs as a
  `Delegate=true` unit that writes `+cpuset` into its own cgroup, which works
  only if systemd has already turned cpuset on at the root. The rule is: pin a
  box with the flags, never with `taskset` around the Guardian. On kernels from
  6.2 on the two intersect and silently narrow the box. The perf harness
  (`perf/harness/runner.py` on the `perf/sandboxing-overhead-measurement`
  branch, which wraps the Guardian in `taskset`) has to switch over before it
  is used again.
- **Only `--run` is pinned.** `--init` and `--cleanup` create no cgroup, so they
  run wherever the caller runs, as with Isolate. They accept the flags and
  ignore them, like every other compat flag, and so does standalone mode.
- **We refuse input Isolate would pass through**: empty lists, strides, `all`,
  `N`. We also fail runs Isolate would silently run unpinned, such as a
  requested core that is offline.
- **The Worker must send the flags only when configured.** Real Isolate has no
  such options and would reject them.
