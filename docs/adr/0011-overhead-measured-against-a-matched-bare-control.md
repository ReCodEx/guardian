# Sandboxing overhead is measured against a matched bare control, not the meta-file

**Status:** accepted

Measuring [sandboxing overhead](../../CONTEXT.md#performance-evaluation) needs a
number for "the same program, not sandboxed". The obvious source is the
Guardian's own **meta-file** — `time`, `time-wall`, `cg-mem` are already emitted
on every `--run`, they are what the ReCodEx Worker actually consumes, and using
them costs no new code.

We do not use them. The meta-file exists in **only one of the two conditions**,
so any comparison built on it varies the instrument together with the treatment:
part of the reported "overhead" would be the difference between our accounting
and whatever was used bare. Instead, every measured run — boxed and bare alike —
is timed by the *same* two instruments: a phase the workload times itself with
`CLOCK_MONOTONIC`, and the harness timing the whole invocation externally. The
meta-file is still recorded, but only as a cross-check against those.

## Consequences

- The control is a purpose-built **matched bare run**: a launcher reproducing
  every *non-isolation* action a task launch performs — constructed `envp`, box
  credentials, the same rlimits, the same working directory, stdio to files, an
  equivalent cgroup. Without that matching the comparison silently includes
  environment size, credentials and rlimit differences, none of which are
  isolation. A **naive bare run** is collected alongside it; the gap between the
  two is reported as a result, being exactly the error an uncontrolled
  comparison would make.
- **Lifecycle cost** (`--init`, `--cleanup`) has no bare counterpart at all —
  nothing outside a sandbox corresponds to creating one — so it is reported as
  an absolute per-task cost and never as a ratio. Ratios are used only where a
  baseline genuinely exists.
- Every workload must carry a self-timed phase, so third-party programs cannot
  be dropped into the attribution set unmodified. The realism anchor is
  therefore measured externally only.
- The dataset is tied to this decision: changing the control invalidates every
  observation rather than allowing a re-analysis.

Independently of this, `time-wall` could not have served as the instrument
anyway — it is derived from `CLOCK_REALTIME` (issue #25).
