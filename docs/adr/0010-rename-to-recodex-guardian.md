---
status: accepted
---

# Rename the tool to Guardian, packaged as `recodex-guardian`

The tool was called **Isolator**. It is now the **Guardian**, shipped as `recodex-guardian`.

## Why the old name failed

`Isolator` was chosen because it sat alongside ReCodEx's `broker` and `worker`. Two problems outweighed that:

- **It reads as a variant of Isolate.** This tool is a *replacement* for Isolate and installs an `isolate` alias (ADR 0002), so both names appear side by side in prose, in `PATH`, and on a deployed host. One-character-family names for two coexisting entities is a documentation and operations hazard, not just an aesthetic one.
- **It is a coinage, not a word.** `isolate` + `-or` names no established thing outside electrical and mechanical contexts, neither of which is confinement.

## Why Guardian

- **Phonetically distinct from `worker`**, which matters because the two are said in the same breath constantly. Three syllables, hard G onset, `-ian` ending. The rule this follows: a name with a W onset or an `-er`/`-or` ending smears into `broker` and `worker`, especially in Czech-accented English — which rules out most of the obvious agent nouns.
- **Needs no gloss for the intended readership.** Czech has *gardián* natively.
- **Clean namespaces** — zero matches in Rocky 9 + EPEL 9 (not even a substring), nothing provides `*/bin/guardian`. Off-domain GitHub projects (Django permissions, Elixir auth) are made irrelevant by the prefix.
- **Accepted cost:** *guardian* connotes protection rather than custody, and confinement is what this tool actually does. Legibility was preferred over precision.

## Why the `recodex-` prefix

`ReCodEx/worker` sets project, target, `CPACK_PACKAGE_NAME` and install target all to `recodex-worker`, landing at `/usr/bin/recodex-worker`; the broker matches. This would plausibly ship in the same COPR, so the artifact follows that convention exactly. The long command name costs nothing here: the Worker invokes us through the `isolate` alias, never by name.

The prefix is applied where namespaces are **shared** and collision is possible — binary, package, man page, `/var/lib`, `/sys/fs/cgroup`, `/run`. Repo-internal names stay bare (`scripts/guardian.sh`, `GUARDIAN_BIN`, `guardian_unit_tests`), mirroring ReCodEx's own split between repo name and artifact name.

## What changed

| | before | after |
|---|---|---|
| prose | the Isolator | the Guardian |
| project / target / binary / package | `isolator` | `recodex-guardian` |
| box tree | `/var/lib/isolator_boxes/<id>` | `/var/lib/recodex-guardian/boxes/<id>` |
| cgroup parent | `/sys/fs/cgroup/isolator_boxes` | `/sys/fs/cgroup/recodex-guardian` |
| locks | `/run/isolator_boxes/locks` | `/run/recodex-guardian/locks` |
| man page | `isolator.1` | `recodex-guardian.1` |
| repo | `rcdx_cntnr` | `recodex-guardian` |

The `isolate` alias, the `Provides: isolate` claim and the setuid install are unchanged — ADR 0001 and ADR 0002 are untouched in substance.

The explicit `boxes/` level under `/var/lib/recodex-guardian` is new. It makes the glossary's *box tree* literal on disk, keeps purge unambiguous (clear `boxes/`, never remove the package-owned root), and leaves room for non-box state without a second path migration.

The same commit dropped the unrelated **container** vocabulary, which the glossary never adopted: `container_core.hpp` → `cores.hpp` (it holds `root_core` and `proxy_core`), ADR 0003's filename → instance-ids, and the `rcdx_isolator` test project.

## Note on history

Earlier ADRs cite build and init scripts that were deleted before this rename (`isolator_init.sh`, `isolator_build.sh`, `isolator_cleanup.sh`). Those citations were **rewritten to the `guardian_*` spelling** for a single consistent vocabulary across the docs. No file ever existed under the new spelling: anything in git history before the rename commit uses `isolator_*`, and that is where to search for it.
