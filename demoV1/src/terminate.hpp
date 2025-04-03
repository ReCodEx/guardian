#ifndef TERMINATE
#define TERMINATE

#include "logs.hpp"

/// @brief Log a critical error and terminate the container.
/// @tparam ...Args 
/// @param fmt Formatted string with the message.
/// @param ...args Args for the formatted string.
template<typename ... Args>
inline void terminate(logs::format_string_t<Args...> fmt, Args&& ... args)
{
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(1);
}

#endif