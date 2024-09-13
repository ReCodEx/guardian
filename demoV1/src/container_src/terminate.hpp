#ifndef TERMINATE
#define TERMINATE

#include "logs.hpp"


template<typename ... Args>
inline void terminate(logs::format_string_t<Args...> fmt, Args&& ... args)
{
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(1);
}

#endif