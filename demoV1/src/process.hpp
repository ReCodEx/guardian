#ifndef PROCESS
#define PROCESS

#include "cgrps.hpp"
#include "config.hpp"
#include "filesystem"
#include "utils.hpp"
#include "logs.hpp"

#include <chrono>
#include <thread>
#include <memory>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/mount.h>

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <sys/syscall.h>    /* Definition of SYS_* constants */
#include <unistd.h>



namespace process_utils
{
    namespace fs = std::filesystem;

/// @brief Convert an std::vector<std::string> to nullptr terminated std::vector<char*>.
/// @param args 
/// @return 
auto convert_to_argv(std::vector<std::string>& args)
{
    std::vector<char*> cstrings{};

    for(auto&& string : args)
        {
            cstrings.emplace_back(string.data());
        }
        cstrings.emplace_back(nullptr);

        return cstrings;
    }
}
#endif