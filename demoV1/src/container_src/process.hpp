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

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <sys/syscall.h>    /* Definition of SYS_* constants */
#include <unistd.h>

template<typename ... Args>
inline void terminate(logs::format_string_t<Args...> fmt, Args&& ... args)
{
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(1);
}

namespace process_utils
{
    #define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

    inline clone_args create_clone_args(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    {
        clone_args args{0};
        args.exit_signal = SIGCHLD;
        args.flags = CLONE_INTO_CGROUP;

        //I guess we will skip trying to allocate a stack for now.

        //args.stack = ptr_to_u64(stack);
        //args.stack_size = task_conf.stack_size;

        args.cgroup = cgrp_fd;
        return args;
    }

    auto convert_to_arg_array(std::vector<std::string>& args)
    {
        std::vector<char*> cstrings{};

        for(auto&& string : args)
        {
            cstrings.push_back(string.data());
        }

        return cstrings;
    }

    inline void cpp_execve(const std::string& exec, std::vector<std::string>& args)
    {
        auto cargs = convert_to_arg_array(args);
        static char *environ[] = { NULL };
        execve(exec.c_str(), cargs.data(), environ);
    }

    inline pid_t clone3_task(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    {
        //pid_t outside_pid = fork();
        //pid_t outside_pid = clone(test, (void*)(args.stack + args.stack_size), SIGCHLD, 0);
        auto args = create_clone_args(task_conf, stack, cgrp_fd);
        return syscall(SYS_clone3, &args, sizeof(clone_args));
    }


}




#endif