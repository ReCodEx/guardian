#ifndef TERMINATE
#define TERMINATE

#include <cstdlib>
#include <format>

#include "logs.hpp"

/// @brief Log a critical error and terminate the current process.
/// @details Exits with code 2 — the Isolator's "internal error" code per
/// Isolate's convention (ADR 0005): 0 = task ran and exited OK, 1 = task ran but
/// result != OK, 2 = the Isolator itself failed (bad flags, setup error,
/// box-not-found). `terminate()` is the internal-error path by definition.
/// @tparam ...Args
/// @param fmt Formatted string with the message.
/// @param ...args Args for the formatted string.
template <typename... Args>
inline void terminate(std::format_string<Args...> fmt, Args&&... args) {
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(2);
}

#endif
