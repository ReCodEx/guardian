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

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <sys/syscall.h>    /* Definition of SYS_* constants */
#include <unistd.h>



namespace process_utils
{
    #define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

    inline clone_args create_clone_args(const config::task_interface& task_conf, void* stack, uint64_t cgrp_fd)
    {
        clone_args args{0};
        args.exit_signal = SIGCHLD;
        args.flags = CLONE_INTO_CGROUP | config::DEFAULT_CLONE_FLAGS;

        //we will skip trying to allocate a stack for now.

        //args.stack = ptr_to_u64(stack);
        //args.stack_size = task_conf.stack_size;

        args.cgroup = cgrp_fd;
        return args;
    }

/*     inline clone_args create_clone_args(const config::namespace_config& config, void* stack, uint64_t cgrp_fd)
    {
        clone_args args{0};
        args.exit_signal = SIGCHLD;
        args.flags = CLONE_INTO_CGROUP | config.get_clone_flags();

        args.cgroup = cgrp_fd;
        return args;
    }
 */
    auto convert_to_argv(std::vector<std::string>& args)
    {
        std::vector<char*> cstrings{};

        for(auto&& string : args)
        {
            cstrings.push_back(string.data());
        }
        cstrings.push_back(nullptr);

        return cstrings;
    }

    inline void cpp_execve(const std::string& exec, std::vector<std::string>& args)
    {
        auto cargs = convert_to_argv(args);
        static char *environ[] = { NULL };
        
        execve(exec.c_str(), cargs.data(), environ);
    }

    inline pid_t clone3_task(const config::task_interface& task_conf, void* stack, uint64_t cgrp_fd)
    {
        //pid_t outside_pid = fork();
        //pid_t outside_pid = clone(test, (void*)(args.stack + args.stack_size), SIGCHLD, 0);
        auto args = create_clone_args(task_conf, stack, cgrp_fd);
        return syscall(SYS_clone3, &args, sizeof(clone_args));
    }

    inline rusage get_children_rusage()
    {
        rusage r_usage;
        auto rv = getrusage(RUSAGE_CHILDREN, &r_usage);
        logs::debug("getrusage returned {}", rv);
        return r_usage;
    }

    inline long rusage_total_time_usec(const rusage& r_usage)
    {
        return (r_usage.ru_utime.tv_sec + r_usage.ru_stime.tv_sec)*1000000 + r_usage.ru_utime.tv_usec + r_usage.ru_utime.tv_usec;
    }

}
#endif