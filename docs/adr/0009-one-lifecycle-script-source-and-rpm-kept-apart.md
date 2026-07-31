---
status: accepted
---

# One lifecycle script, with source installs and RPM installs kept strictly apart

`scripts/isolator.sh MODE` (`build`, `package`, `install`, `uninstall`, `purge`) is the
single entry point for producing and removing the Isolator, replacing
`isolator_build.sh`, `isolator_cleanup.sh`, and the hand-run `cmake --install` /
`rm`-the-manifest sequences the README used to document. The script **produces** RPMs
(`package` runs CPack) but never installs or removes them: `dnf` owns that lifecycle,
and `uninstall` runs `rpm -qf` on the target binary and **refuses** when a package owns
it, pointing at `dnf remove` instead.

That refusal is the load-bearing part. A manifest-driven removal on an RPM-installed
host deletes RPM-owned files behind `rpm`'s back, leaving the rpm database convinced
`isolator` is still installed and the next transaction on the package broken. The
alternative — teaching `uninstall` to detect ownership and shell out to `dnf remove` —
was rejected because wrapping a package manager pulls distro assumptions, extra sudo
prompts and new failure modes into a helper script, to save one well-known command.

## Consequences

- **`uninstall` needs the build tree that performed the install**, because it reads
  `install_manifest.txt`. Deleting or reconfiguring the tree first means removing the
  files by hand. Accepted deliberately over computing the path set from the prefix: the
  manifest cannot delete something that was never installed.
- **The `isolate` alias needs a special case.** It is created by an `install(CODE
  file(CREATE_LINK ...))` step, and CMake records only `install(TARGETS/FILES/...)` in
  the manifest — there is no first-class "install a symlink" that lands there. So
  `uninstall` removes `<bindir>/isolate` explicitly, deriving `<bindir>` from the
  manifest's own `isolator` entry. Without that line the manifest removal leaves a
  **dangling `isolate` first on `PATH`**, so anything invoking it (the Worker included)
  fails confusingly instead of falling through.
- **The build tree's `CMakeCache.txt` is the memory for configure-time options**
  (`ISOLATE_ALIAS`, prefix, build type). Modes inherit it and only reconfigure when a
  flag actually changes something, so `build --alias` followed by a plain `install`
  keeps the alias. Defaults are uniform across modes (`Release`, `/usr`, alias off) —
  per-mode defaults would silently reconfigure and rebuild on every crossing from
  `build` to `install`.
- **`purge` is host-wide and `--cleanup` is per-box** — see the glossary entry in
  `CONTEXT.md`. `uninstall` alone only `rmdir`s the box tree when it is empty, mirroring
  what `dnf remove` does with the package's `%dir`; killing live boxes takes an explicit
  `--purge`.
