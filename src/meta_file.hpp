#ifndef META_FILE
#define META_FILE

#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include "config.hpp"
#include "logs.hpp"
#include "meta_sink.hpp"

/// @brief Compatibility-mode result channel (ADR 0005, C1): the fixed-size
/// `task_stats` record the proxy pushes up the pipe, and the Isolate
/// `key:value` meta-file the root process writes from it.
///
/// This is the compat-mode counterpart of standalone's `generate_stats_yaml`
/// (our YAML format); it centralizes the Isolate meta format and the
/// `exit_status` -> Isolate-code mapping in one place, on the root side.
namespace meta {
    namespace fs = std::filesystem;

    // The record crosses a clone3 boundary as raw bytes (same binary both
    // ends), so it must have no non-trivial members; the day someone adds a
    // std::string to task_stats, this breaks the build rather than the wire.
    static_assert(std::is_trivially_copyable_v<config::task_stats>,
                  "task_stats travels the meta pipe as a fixed-size blob");

    /// @brief Isolate meta `status:` classification of a completed task.
    /// @details `ok` corresponds to *no* `status:` line (Isolate omits it on
    /// success). Memory-limit hits and signals both map to `SG`; either time
    /// limit maps to `TO`; a plain non-zero exit maps to `RE` (ADR 0005 C1).
    enum class result_code { ok, re, sg, to };

    inline result_code classify(const config::task_stats& s) {
        using config::exit_status;
        if (s.exit == exit_status::WALL_TIME_EXCEEDED ||
            s.exit == exit_status::CPU_TIME_EXCEEDED) {
            return result_code::to;
        }
        if (s.exit == exit_status::MEMORY_LIMIT_EXCEEDED) {
            return result_code::sg;  // OOM-kill; cg-oom-killed is the real
                                     // discriminator (deferred metric)
        }
        if (s.signalled) {
            return result_code::sg;
        }
        if (s.exited_normally && s.exit_code != 0) {
            return result_code::re;
        }
        return result_code::ok;
    }

    inline std::string_view code_str(result_code rc) {
        switch (rc) {
            case result_code::re:
                return "RE";
            case result_code::sg:
                return "SG";
            case result_code::to:
                return "TO";
            case result_code::ok:
                return "";
        }
        return "";
    }

    /// @brief Isolator exit code for a completed run: 0 iff OK, else 1
    /// (Isolate's convention; 2 is reserved for the Isolator's own failure).
    inline int result_exit_code(const config::task_stats& s) {
        return classify(s) == result_code::ok ? 0 : 1;
    }

    /// @brief Write the Isolate-format meta-file for a completed task.
    /// @details Emits `status:` (omitted on OK), `exitcode:` (when the task
    /// exited normally) and `exitsig:` (when it died on a signal). The metric
    /// lines (`time`, `cg-mem`, `max-rss`, …) are the deferred rows of the same
    /// table (ADR 0005 C1). Best-effort: never throws, never `terminate()`s.
    inline void write_result(const fs::path& path,
                             const config::task_stats& s) {
        std::string out;
        if (auto rc = classify(s); rc != result_code::ok) {
            out += std::format("status:{}\n", code_str(rc));
        }
        if (s.exited_normally) {
            out += std::format("exitcode:{}\n", s.exit_code);
        }
        if (s.signalled) {
            out += std::format("exitsig:{}\n", s.signal);
        }
        detail::write_file(path, out);
    }

    /// @brief Push one task's result up the meta pipe (proxy side).
    /// @details Writes the `task_stats` as a fixed-size blob. Best-effort: a
    /// write error just means the proxy exits without a complete record, which
    /// root reads as an internal error (empty/partial ⇒ `status:XX`, exit 2).
    inline void write_record(int fd, config::task_stats s) {
        s.config_ = nullptr;  // hygiene: no stale pointer bits on the wire
        const char* p = reinterpret_cast<const char*>(&s);
        std::size_t left = sizeof(s);
        while (left > 0) {
            ssize_t n = ::write(fd, p, left);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                logs::error("Failed writing meta record to pipe, errno: {}",
                            errno);
                return;
            }
            p += n;
            left -= static_cast<std::size_t>(n);
        }
    }

    /// @brief Outcome of reading the meta record (root side). The fixed record
    /// size is the exit-code signal: a full read is a result, EOF means the
    /// proxy died before reporting, a short read means it died mid-write.
    enum class read_status { complete, empty, partial };

    struct read_result {
        read_status status;
        config::task_stats stats{};
    };

    /// @brief Read one task's result from the meta pipe (root side).
    /// @details Loops until it has `sizeof(task_stats)` bytes (complete), hits
    /// EOF with nothing read (empty), or EOF mid-record (partial).
    inline read_result read_record(int fd) {
        config::task_stats s{};
        char* p = reinterpret_cast<char*>(&s);
        std::size_t got = 0;
        while (got < sizeof(s)) {
            ssize_t n = ::read(fd, p + got, sizeof(s) - got);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                logs::error("Failed reading meta record from pipe, errno: {}",
                            errno);
                break;
            }
            if (n == 0) {
                break;  // EOF
            }
            got += static_cast<std::size_t>(n);
        }

        if (got == sizeof(s)) {
            return {.status = read_status::complete, .stats = s};
        }
        if (got == 0) {
            return {.status = read_status::empty, .stats = s};
        }
        return {.status = read_status::partial, .stats = s};
    }
}  // namespace meta

#endif
