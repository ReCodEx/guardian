# Memory reporting in the compatibility meta-file

The compat meta-file reports two independent memory signals, matching Isolate:
`cg-mem` (cgroup `memory.peak`, subtree-wide peak) and `max-rss` (rusage
`ru_maxrss`, per-process peak). Both are emitted in **binary KB** (bytes >> 10),
end-to-end consistent with how the Worker sets `--cg-mem=<KB>` and how
`memory.max` is derived.

**When `memory.peak` is unavailable** (the el9 / kernel 5.14 target may not
backport it — see `docs/adr/0004`), we **omit the `cg-mem` line entirely** and
rely on `max-rss` as the memory signal, rather than synthesizing a fallback
value.

This refines ADR-0004, which suggested falling back to sampling
`memory.current` or `getrusage` `max-rss` *for `cg-mem` itself*. We don't,
because the ReCodEx Worker parses `cg-mem` with `std::stoul(second)` in a loop
with **no exception handling** (`worker/src/sandbox/isolate_sandbox.cpp`):

- a non-numeric sentinel (`n/a`, empty, `-`) throws `std::invalid_argument`,
  which is uncaught and **crashes the parse of the whole meta-file**;
- `-1` does not throw — `std::stoul("-1")` **wraps to `ULONG_MAX`** (~18 EB),
  a silent catastrophic misreport;
- any non-zero integer reads as a real memory figure, so there is no safe
  out-of-band "not measured" value.

Omission is therefore the only honest way to say "cgroup memory was not
measured" — and it is exactly Isolate's own behavior (its `?memory.peak` read
leaves `cg-mem` unemitted when the file is absent). The Worker's parse loop
simply never assigns `results.memory`, which keeps its default. `max-rss`,
sourced from rusage, is always present and carries the memory signal in the
degraded case.
