#ifndef META_SINK
#define META_SINK

#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string_view>
#include <utility>

#include "logs.hpp"

/// @brief The internal-error meta channel (ADR 0005, C2): the process-global
/// meta-sink `terminate()` writes `status:XX` to, plus the best-effort
/// key:value file writers it and the root process share.
///
/// Deliberately config-free — depends only on a path and a string, never on
/// `task_stats`. `terminate.hpp` includes this; `config.hpp` includes
/// `terminate.hpp`, so folding this into `meta_file.hpp` (which pulls
/// `config.hpp`) would make the include graph cyclic. The `task_stats`-shaped
/// half of the meta writer lives in `meta_file.hpp`, which includes this.
namespace meta {
    namespace fs = std::filesystem;

    namespace detail {
        /// @brief Write @p body to @p path, best-effort. On failure it logs and
        /// returns — it must never `terminate()`, which invokes the meta-sink
        /// and would re-enter the writer and recurse.
        inline void write_file(const fs::path& path, std::string_view body) {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) {
                logs::error("Could not open meta-file {} for writing",
                            path.string());
                return;
            }
            f.write(body.data(), static_cast<std::streamsize>(body.size()));
            if (!f) {
                logs::error("Failed writing meta-file {}", path.string());
            }
        }
    }  // namespace detail

    /// @brief Write the Isolate internal-error meta-file (`status:XX`).
    /// @details Used by the root process for an empty/partial meta pipe and by
    /// the meta-sink below. Best-effort: never throws, never `terminate()`s.
    inline void write_internal_error(const fs::path& path,
                                     std::string_view message) {
        detail::write_file(path,
                           std::format("status:XX\nmessage:{}\n", message));
    }

    /// @brief Process-global meta-sink: the `--meta` path `terminate()` should
    /// write `status:XX` to, or empty when unarmed.
    /// @details A function-local `static` (the `logs::current_level()` idiom),
    /// so this stays header-only. Armed once by `root_core::run()` when a
    /// compat phase was given `--meta`; disabled by the proxy immediately after
    /// `clone3` so only the root process ever writes the host meta-file.
    inline std::optional<fs::path>& sink() {
        static std::optional<fs::path> path;
        return path;
    }

    inline void arm_sink(fs::path path) { sink() = std::move(path); }

    inline void disarm_sink() { sink().reset(); }

    /// @brief If the sink is armed, write a `status:XX` meta with @p message.
    /// @details Called from `terminate()`. Best-effort and a no-op when
    /// unarmed (standalone, pre-arm, or a phase given no `--meta`), so the
    /// exit(2)-only behavior is preserved everywhere the sink is not set.
    inline void fire_sink(std::string_view message) {
        if (sink()) {
            write_internal_error(*sink(), message);
        }
    }
}  // namespace meta

#endif
