# setuid-root install with an `isolate` alias

The ReCodEx Worker runs as the non-root `recodex` user yet `execvp`s a PATH-resolved binary named `isolate` that must gain root to create namespaces and cgroups. To preserve "replace only the command," we install the Guardian **setuid root** (`mode 4755`), exactly as upstream Isolate does, and expose an `isolate`-named entrypoint (symlink) so PATH resolves to us. The package **`Provides: isolate`** (satisfying the Worker RPM's `Requires: isolate`, ADR 0004) and prevents coexistence with the upstream `isolate` package by **owning the same `/usr/bin/isolate` path** — RPM refuses to install two packages that own one file — rather than by an explicit `Conflicts: isolate`, which would self-conflict against our own `Provides`.

We accept the larger setuid attack surface (a C++ binary linking Boost / yaml-cpp / spdlog versus Isolate's hardened C) because no alternative preserves invocation by a non-root Worker without sudo or a Worker patch. We mitigate by building with the same compiler-hardening flags Isolate uses — the full set: `-fstack-protector-strong -fstack-clash-protection -fPIE -pie -D_FORTIFY_SOURCE=3` (fortify gated to optimized builds, where it is not a no-op) plus linker flags `-Wl,-z,{nodlopen,noexecstack,relro,now}` — and statically linking libstdc++/libgcc so the binary carries no SCL runtime dependency (ADR 0004).

The alias is packaging, not behaviour, so it is a build-time switch:
`-DISOLATE_ALIAS` (default `OFF`) gates the symlink, the `man isolate` redirect
and the RPM `Provides: isolate` together. It is opt-in because claiming
`/usr/bin/isolate` displaces upstream Isolate on the host — a default build owns
no such path, installs beside upstream Isolate, and serves standalone `--yaml`
use. A Worker host builds with `-DISOLATE_ALIAS=ON` to get the drop-in path and
satisfy the Worker RPM's `Requires: isolate`.

## Consequences

- Credential handling must respect the real-vs-effective-uid split under setuid: privileged setup runs at euid 0, and the drop-to-box-uid / restore paths must be correct when the real uid is the unprivileged `recodex` user (current code reads `getuid()` at `credentials.hpp`, which needs an audit).
- To shrink the setuid attack surface, trim third-party libraries from the privileged binary:
  - Drop the compiled **Boost.program_options** dependency and hand-roll CLI parsing with `getopt_long` (glibc, zero-dependency, as upstream Isolate does). Dovetails with the issue #1 front-end rewrite, which replaces Boost's `variables_map` plumbing with a single flat `cli_options` struct feeding each config class's constructor. The vestigial bare-CLI standalone path (building a task from loose `--bin`/`--mem`/… flags with no `--yaml`) is **dropped** in the same change — no test exercised it, and YAML is now the sole standalone interface; only `--yaml` standalone and the three-phase compatibility mode remain.
  - Drop **spdlog** (and its bundled **fmt**); re-implement the thin `logs::` wrapper over C++23 `std::format` writing to **stderr** via `fwrite`. We use `std::format`+`fwrite` rather than `std::print`: `std::print` requires GCC 14's experimental `<print>` link support (`-lstdc++exp`) on the gcc-toolset-14 target (ADR 0004), whereas `std::format` is solidly supported. Logging to stderr (not stdout) is load-bearing — `--init` prints the box-root path to stdout for the Worker to parse, so no log byte may touch stdout.
  - **Keep yaml-cpp** — it is the canonical C++ YAML library, is genuinely needed for standalone config + the test suites, and is never reached on the drop-in path (the Worker uses CLI flags and a `key:value` text meta-file, no YAML). This leaves the binary's third-party footprint at just yaml-cpp.
