# isolate-compatible drop-in mode over the single-shot core

To replace Isolate in the ReCodEx Worker without patching the Worker, the Isolator must speak Isolate's interface verbatim: the Worker `execvp`s a binary named `isolate` in three independent phases (`--init` → populate box → `--run -- prog args` → `--cleanup`), keyed by a caller-supplied `--box-id`, and parses Isolate's text meta-file. We therefore add an **isolate-compatibility mode** (a three-phase CLI front-end emitting Isolate-format meta and `0/1/other` exit codes) *alongside* the existing single-shot YAML **standalone mode**, rather than building an external adapter shim or forking the Worker.

The state shared between the three separate process invocations is **on-disk**: the persistent box directory (and its writable `/box`), plus a small per-box lock record. Per-box uid (`box_id + 60000`) and cgroup path are pure functions of `--box-id`, recomputed each invocation — so no daemon and no persisted *resource* metadata is needed.

We **do** implement Isolate's per-box `flock`'d lock from the start, because mutual exclusion is the one genuinely valuable property it provides: a second invocation on the same `--box-id` must fail rather than corrupt a box in use. (The Worker already serializes phases per box-id, but the guard is cheap and also protects standalone / multi-tenant use.) The lock additionally carries an `is_initialized` bit so `--run` can refuse a box that was never `--init`'d. We deliberately **drop** Isolate's other two lock fields: `cg_enabled` is moot (we are always cgroup-v2), and `owner_uid` enforcement is moot in this deployment (a single `recodex` user under setuid). Note Isolate's `--quota` is not persisted in the lock either — it is applied to the filesystem at `--init` via `quotactl` and simply persists there.

## Considered options

- **External adapter shim** translating Isolate CLI → our YAML — rejected: a second moving part, and the three-phase/single-shot state still has to live somewhere.
- **Patch the Worker** with a native YAML sandbox backend — rejected: we'd own a Worker fork and it would no longer be "replace only the command."

## Consequences

- The single-shot YAML mode is retained unchanged for the test suites.
- Implement only the flag set the Worker actually emits; accept-and-ignore cosmetic flags (`--cg`, `--cg-timing`); reject unknown flags loudly so divergence is visible.
- Default directory rules must match Isolate's set exactly, anchored on a writable `/box`.
- The lock record lives on tmpfs at `/run/isolate_boxes/locks/<box-id>`, deliberately *outside* the persistent box tree under `/isolate_boxes`: `--cleanup` `rm -rf`s the box dir, so the lock must survive that, and a stale lock after reboot is harmless (tmpfs is empty, the box tree it guards is also gone). Following Isolate, `--cleanup` `ftruncate`s the lock to zero (clearing the magic → "uninitialized") rather than `unlink`ing it, to avoid a re-open/re-create race on the same path. The on-disk layout (per-phase responsibility split, who creates the cgroup) is detailed in ADR 0005.
