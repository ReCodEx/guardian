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

    // /// @brief Change root and unmount the old one.
    // /// @param new_root Path of new root.
    // /// @param put_old Directory under the new root where the old root is mounted during pivot_root() syscall.
    // inline void pivot_root(const fs::path& new_root, const fs::path& put_old)
    // {
    //     if(syscall(SYS_pivot_root, new_root.c_str(), put_old.c_str()))
    //         { terminate("pivot_root failed, errno: {}", errno); }
    //     chdir("/");
    //     if(umount2("/old_root", MNT_DETACH))
    //         { logs::error("umount on old root failed, errno: {}", errno); }
    // }

    // /// @brief 
    // /// @param task_conf 
    // /// @param stack 
    // /// @param cgrp_fd 
    // /// @return 
    // inline clone_args task_clone_args(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    // {
    //     clone_args args{0};
    //     args.exit_signal = SIGCHLD;
    //     args.flags = CLONE_INTO_CGROUP;

    //     //we will skip trying to allocate a stack for now.

    //     //args.stack = ptr_to_u64(stack);
    //     //args.stack_size = task_conf.stack_size;

    //     args.cgroup = cgrp_fd;
    //     return args;
    // }
    
    
    // inline clone_args proxy_clone_args(const config::proxy_config& proxy_conf, void* stack, uint64_t cgrp_fd)
    // {
    //     clone_args args{0};
    //     args.exit_signal = SIGCHLD;
    //     args.flags = config::DEFAULT_CLONE_FLAGS | CLONE_INTO_CGROUP;

    //     args.cgroup = cgrp_fd;
    //     return args; 
    // }

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

    // inline void cpp_execve(const std::string& exec, std::vector<std::string>& args)
    // {
    //     auto cargs = convert_to_argv(args);
    //     static char *environ[] = { NULL };
        
    //     execve(exec.c_str(), cargs.data(), environ);
    // }

    // inline pid_t clone3_task(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    // {
    //     auto args = task_clone_args(task_conf, stack, cgrp_fd);
    //     return syscall(SYS_clone3, &args, sizeof(clone_args));
    // }
    
    // inline pid_t clone3_proxy(const config::proxy_config& config, void* stack, uint64_t cgrp_fd)
    // {
    //     auto args = proxy_clone_args(config, stack, cgrp_fd);
    //     return syscall(SYS_clone3, &args, sizeof(clone_args));
    // }

    // inline rusage get_children_rusage()
    // {
    //     rusage r_usage;
    //     if(getrusage(RUSAGE_CHILDREN, &r_usage))
    //         { logs::error("getrusage() failed"); }
    //     return r_usage;
    // }

    // /// @brief Get total CPU time used by the process from rusage structure.
    // /// @param r_usage 
    // /// @return CPU time in microseconds.
    // inline long rusage_total_time_usec(const rusage& r_usage)
    // {
    //     return (r_usage.ru_utime.tv_sec + r_usage.ru_stime.tv_sec)*1000000 + r_usage.ru_utime.tv_usec + r_usage.ru_utime.tv_usec;
    // }

}
#endif