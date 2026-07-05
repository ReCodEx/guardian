---
status: proposed
---

# Target platform: Rocky Linux 9 / RPM, built with gcc-toolset

ReCodEx Worker cluster nodes run **Rocky Linux 9** (el9, kernel 5.14, system GCC 11.5). The Worker is itself RPM-packaged and `Requires: isolate` as an RPM. We therefore target **RPM on el9** as the primary package (via CPack's RPM generator), and keep `cmake --install` for source builds. A `.deb` can be added later from the same CMake config if any host runs Ubuntu.

The project is C++23, but the el9 system compiler (GCC 11.5) only partially supports it. We build under **gcc-toolset-14** (added as a `BuildRequires`, mirroring the `/opt/rh/gcc-toolset-14` already present in the environment) and **statically link libstdc++ / libgcc** (`-static-libstdc++ -static-libgcc`) so the setuid binary is self-contained and does not depend on the SCL runtime at execution time.

## Open item (pending cluster verification)

`memory.peak` — which gives accurate `cg-mem` / peak-memory reporting — landed in mainline kernel 5.19; el9 runs 5.14 and may or may not backport it. If it is absent on the target nodes, the meta-file's memory reporting needs a documented fallback (sample `memory.current` at task exit, or lean on `getrusage` `max-rss`). The exact gcc-toolset version and `memory.peak` availability are to be confirmed against the actual cluster software.

**Refined by `docs/adr/0006`:** for the `cg-mem` key specifically we do *not* synthesize a fallback value — we omit the line and rely on `max-rss`, because the Worker's `std::stoul` parser makes any sentinel unsafe.
