---
status: proposed
---

# Collision-safe instance IDs via atomic claim

In standalone mode the instance ID is not supplied by a caller, so it must be generated. Hashing an instance name + time is used only as a **candidate generator** (readable names, good spread); it does **not** guarantee uniqueness — the id space is small because `box_uid = box_id + 60000`, so the birthday bound bites, and a collision today is a hard `terminate()` at box-dir creation.

The uniqueness guarantee is instead the **atomic claim**: `mkdir` of the box directory either succeeds (the id, and the derived uid + cgroup, are ours) or fails `EEXIST` (try the next candidate). No allocator daemon is required.

This refines the weakness recorded in the thesis ("random 1–5000 if unspecified; a synchronized allocator service is future work"): the daemon is rejected as unnecessary, and pure hashing is rejected as merely probabilistic.

This does not apply to isolate-compatibility mode, where the Worker supplies a `--box-id` that is unique per worker thread by construction.
