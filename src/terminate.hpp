#ifndef TERMINATE
#define TERMINATE

#include <cstdlib>
#include <format>

#include "logs.hpp"

/// @brief Log a critical error and terminate the current process.
/// @tparam ...Args
/// @param fmt Formatted string with the message.
/// @param ...args Args for the formatted string.
template <typename... Args>
inline void terminate(std::format_string<Args...> fmt, Args&&... args) {
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(1);
}

#endif
