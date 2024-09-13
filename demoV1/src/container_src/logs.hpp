#ifndef LOGS
#define LOGS

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace logs
{
    template<typename ... Args>
    using format_string_t = spdlog::format_string_t<Args...>;

    inline void init_default_logger()
    {
        //auto my_logger = spdlog::basic_logger_mt("basic_logger", "logs/log.txt");
        //spdlog::set_default_logger(my_logger);
    }

    template <typename... Args>
    inline void critical(format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_CRITICAL(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void error(spdlog::format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_ERROR(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void warn(spdlog::format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_WARN(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void info(spdlog::format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_INFO(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void debug(spdlog::format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_DEBUG(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void trace(spdlog::format_string_t<Args...> fmt, Args &&...args) 
    {
        SPDLOG_TRACE(fmt, std::forward<Args>(args)...);
    }
    
}

#endif

