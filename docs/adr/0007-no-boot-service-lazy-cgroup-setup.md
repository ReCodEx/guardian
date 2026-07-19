---
status: accepted
---

# No boot service: the cgroup tree is self-arranged at `--run`

The Isolator needs its per-instance cgroup parent `/sys/fs/cgroup/isolator_boxes`
to exist with the `cpu`/`memory`/`pids` controllers enabled in its
`cgroup.subtree_control` before any `<box-id>` cgroup can be created and given
limits. Upstream Isolate arranges this out-of-band at boot (a systemd **slice**
plus the `isolate-cg-keeper` daemon). We deliberately ship **no boot service and
no separate helper**: `--run` arranges the cgroup tree it needs, lazily and
idempotently, the first time it is invoked after boot.

This is a small, symmetric extension of what `root_cgroup_manager::run()`
(`cgrps.hpp`) already does — it already enables controllers on the **root**
`/sys/fs/cgroup` and on the **instance** `…/isolator_boxes/<id>` on every run. The
only gap was the **intermediate** `isolator_boxes` level: ensure the parent
directory exists (`create_directories`, idempotent) and enable its
`subtree_control` before enabling the instance level. That gap had been
externalized into `scripts/isolator_init.sh` (which the workload harness ran via
`tests/workload/conftest.py`); folding it into `--run` removes the external step.

## Considered options

- **Oneshot systemd service** running a shell script or a dedicated
  `--init-system` binary mode that does the `mkdir` + `subtree_control` writes
  once at boot. Rejected: it reintroduces exactly the boot-vs-run temporal split
  that ADR 0005 rejected for our daemonless model, adds an install artifact and a
  "did you enable the service?" operational footgun, and — decisively — buys
  almost nothing, because `--run` **already** writes the root `subtree_control`
  on every invocation. The one-time-at-boot framing does not reduce the
  systemd-collision surface it was imagined to, since that write already happens
  per-run today.
- **Separate helper binary** (à la `isolate-cg-keeper`). Rejected for the same
  reason plus the second-artifact cost; `cg-keeper`'s separation earns its keep
  only for the long-running delegation-daemon/slice model we are deferring.
- **Self-arrange in `--run`** (chosen). Zero-config install (`dnf install` → it
  works), consistent with ADR 0005's "cgroup path is a pure function of box-id,
  recomputed each phase," and lets the test harness drop its `isolator_init.sh`
  dependency.

## Consequences

- **`--run` gains ~3 lines of idempotent cgroup-parent setup** (ensure
  `isolator_boxes` exists, enable its `subtree_control`) ahead of the existing
  instance-level enable. It must tolerate a concurrent peer having already
  created/enabled the shared parent (create-if-absent; enabling an
  already-enabled controller in cgroup v2 is a no-op).
- **No unit file, no `--init-system` mode, no install scriptlet** enter the RPM.
  `/var/lib/isolator_boxes` remains lazily created by the per-instance path and
  owned by the RPM as a `%dir`; `/run/isolator_boxes/locks` remains lazily created
  by `box_lock`. Nothing about the install requires a boot step.
- **The systemd-slice escalation risk is unchanged** — it was never about the
  service. Touching the root `/sys/fs/cgroup/cgroup.subtree_control` without a
  systemd slice can collide with systemd's cgroup ownership on el9; `--run` does
  this regardless (and already did). A collision on the target cluster remains the
  trigger to adopt a systemd slice (deferred per issue #14), at which point a
  boot-time delegation step may become genuinely necessary.
