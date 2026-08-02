#ifndef TERMINATE
#define TERMINATE

#include <cstdlib>
#include <format>
#include <string>

#include "logs.hpp"
#include "meta_sink.hpp"

/// @brief Log a critical error and terminate the current process.
/// @details Exits with code 2 — the Guardian's "internal error" code per
/// Isolate's convention (ADR 0005): 0 = task ran and exited OK, 1 = task ran but
/// result != OK, 2 = the Guardian itself failed (bad flags, setup error,
/// box-not-found). `terminate()` is the internal-error path by definition.
/// @tparam ...Args
/// @param fmt Formatted string with the message.
/// @param ...args Args for the formatted string.
template <typename... Args>
[[noreturn]] inline void terminate(std::format_string<Args...> fmt,
                                   Args&&... args) {
    // Format once, then send the same text to two sinks: stderr (always) and,
    // in compat mode, the Isolate meta-file as status:XX (ADR 0005 C2). The
    // meta-sink is a no-op unless root armed it, so standalone and the proxy
    // (which disables it right after clone3) keep the plain exit(2) behavior.
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    logs::critical("{}", msg);
    meta::fire_sink(msg);
    exit(2);
}

#endif
