#ifndef LOGS
#define LOGS

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace logs {
    /// @brief Severity levels, ordered ascending. A message is emitted only if
    /// its level is >= the current process level.
    enum class level { trace = 0, debug, info, warn, error, critical, off };

    /// @brief Process-global current log level.
    /// @details Defaults to `info`. `--debug` lowers this to `debug`.
    inline level& current_level() {
        static level lvl = level::info;
        return lvl;
    }

    inline void set_level(level lvl) { current_level() = lvl; }

    namespace detail {
        inline std::string_view level_name(level l) {
            switch (l) {
                case level::trace:
                    return "trace";
                case level::debug:
                    return "debug";
                case level::info:
                    return "info";
                case level::warn:
                    return "warn";
                case level::error:
                    return "error";
                case level::critical:
                    return "critical";
                default:
                    return "off";
            }
        }

        /// @brief Format and write one line to stderr, gated by the current
        /// level.
        /// @note stderr only: stdout is reserved for the `--init` box-root
        /// path.
        inline void log(level msg_level, std::string_view msg) {
            if (msg_level < current_level()) {
                return;
            }
            auto now = std::chrono::floor<std::chrono::milliseconds>(
                std::chrono::system_clock::now());
            std::string line = std::format("[{:%F %T}] [{}] {}\n", now,
                                           level_name(msg_level), msg);
            std::fwrite(line.data(), 1, line.size(), stderr);
        }
    }  // namespace detail

    template <typename... Args>
    inline void critical(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::critical,
                    std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void error(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::error,
                    std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void warn(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void info(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void debug(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::debug,
                    std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline void trace(std::format_string<Args...> fmt, Args&&... args) {
        detail::log(level::trace,
                    std::format(fmt, std::forward<Args>(args)...));
    }
}  // namespace logs

#endif
