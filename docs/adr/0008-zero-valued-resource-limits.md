---
status: accepted
---

# A zero resource limit is enforced or refused, never read as "no limit"

Isolate guards most of its resource caps on a non-zero value, so passing `0`
silently means *unlimited* (`isolate/isolate.c:790-811`, `cg.c:236`,
`rules.c:510`):

```c
if (memory_limit)     RLIM(AS,     memory_limit * 1024);
if (fsize_limit)      RLIM(FSIZE,  fsize_limit * 1024);
if (open_file_limit)  RLIM(NOFILE, open_file_limit);
RLIM(STACK, (stack_limit ? (rlim_t)stack_limit * 1024 : RLIM_INFINITY));
RLIM(CORE,  (rlim_t)core_limit * 1024);   /* always applied; 0 is a real limit */
if (max_processes)    RLIM(NPROC,  max_processes);
```

We store each cap as a `std::optional` and applied every one behind "was it
set?", never "is it non-zero?". That produced a live bug (#21): the bare
`--processes` the Worker emits on *every* sandboxed task — whenever its
`parallel` limit is 0, the shipped default — parses to `0`, engaged the optional,
and wrote `pids.max = 0`, so nothing could fork. It also left a family of
zero-valued flags applying literal zeros where Isolate applies nothing (#22).

We do **not** adopt Isolate's silent sentinel. A zero is either a real limit we
enforce, or an input we refuse — never a value that quietly disables the cap:

- **Enforced literally**: `mem`/`cg-mem`, `fsize`, `open-files`, `as-size`,
  `quota`. A caller asking for zero gets zero. Strict, occasionally
  pathological (`--fsize=0` forbids all writes), but well-defined.
- **Refused** (usage error, exit 2): `stack` and `processes`, where a literal
  zero is not strict but *meaningless*. `RLIMIT_STACK = 0` leaves the task unable
  to `execve` at all, so every such invocation would fail; and `pids.max = 0` only
  duplicates `--processes=1`, because the pids controller fails subsequent forks
  rather than killing the task already in the cgroup. Neither expresses anything
  a caller could want.
- **No rule needed** for `core`: 0 (no core dumps) is a genuine limit for both of
  us, and Isolate applies it unconditionally too.

Asking for *no* limit means **omitting** the flag — or, for processes, the bare
`--processes`, which stays unlimited because that is what the Worker emits and
because Isolate's own parse cannot distinguish it from `--processes=0` either.

One cap is applied **unconditionally**: an unset `stack` becomes
`RLIM_INFINITY`, mirroring Isolate's ternary. This is not cosmetic. The Worker
omits `--stack` whenever its `stack-size` limit is 0 (its default), so the usual
configuration is "no stack flag at all" — and leaving the caller's ~8 MiB in place
means a deeply-recursive solution that passes on real ReCodEx gets `SIGSEGV`
under us. A verdict flip, with nothing zero-valued ever passed.

## Considered options

- **Adopt Isolate's sentinel everywhere** (`0` ⇒ unlimited for the whole family).
  Faithful, and never fails an input Isolate accepts. Rejected: it makes the most
  restrictive value a caller can write silently mean the *least* restrictive
  behaviour, which is a trap in a component whose entire job is enforcing limits.
- **Enforce every zero literally.** Uniform and simple to state. Rejected for
  `stack` and `processes` specifically: it buys an unrunnable task and a duplicate
  of `=1`, and for processes it would additionally require splitting bare
  `--processes` from `--processes=0` in the parse — extra state for a form nothing
  emits.
- **Enforce where meaningful, refuse where not** (chosen). Keeps one principle —
  we never reinterpret a zero — while declining to implement two behaviours with
  no use. Rejection lives at the two entry points (`cli::parse_positive_size` for
  compat, `resource_limits::require_positive` for standalone YAML), so the zero
  sentinel can only originate from the bare `--processes`.

## Consequences

- **We refuse four invocations Isolate accepts**: `--stack=0`, `--processes=0`,
  and their standalone `stack: 0` / `processes: 0` equivalents, all exit 2 with a
  message naming the flag. Today's Worker emits none of them — it guards `--stack`
  on non-zero (`worker/src/sandbox/isolate_sandbox.cpp:331`) and sends the bare
  form for processes — so this is unreachable through ReCodEx. If a future Worker
  version did emit one, we fail loudly where Isolate ran the job; that is the
  accepted risk, taken because a specific usage error is easier to diagnose than a
  silently different limit.
- **`resource_limits`' invariant tightens**: an engaged optional always means a
  real cap. Enforcement sites keep their plain "is it set?" test, and 0 is handled
  once per entry point instead of at each application site.
- **Every task now runs with an unlimited stack unless capped**, a change to the
  default sandbox environment rather than to any flag's meaning. Deep recursion
  that previously died at the inherited limit now succeeds — which is the point,
  but it also means stack exhaustion is bounded by `memory.max` instead, so a
  runaway recursion surfaces as a memory verdict rather than a segfault.
- **`--processes` and `--fsize` now disagree about zero** (refused vs. enforced).
  That asymmetry is deliberate and follows from whether a literal zero means
  anything, not from which flag it is.
