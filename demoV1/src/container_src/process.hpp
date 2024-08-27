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
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/mman.h>

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <sys/syscall.h>    /* Definition of SYS_* constants */
#include <unistd.h>

#include "spdlog/spdlog.h"

template<typename ... Args>
inline void terminate(logs::format_string_t<Args...> fmt, Args&& ... args)
{
    logs::critical(fmt, std::forward<Args>(args)...);
    exit(1);
}


namespace clone_utils
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

    inline pid_t clone3(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    {
        //pid_t outside_pid = fork();
        //pid_t outside_pid = clone(test, (void*)(args.stack + args.stack_size), SIGCHLD, 0);
        auto args = create_clone_args(task_conf, stack, cgrp_fd);
        return syscall(SYS_clone3, &args, sizeof(clone_args));
    }


}

int test(void* arg)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(25000));
    return 0;
}

namespace tasks
{
    using namespace clone_utils;
    namespace fs = std::filesystem;

    class task_t
    {
        
    public:
        task_t(config::task_config* conf) : task_(conf) 
        {
            //stack_ = std::aligned_alloc(task_->stack_size, task_->stack_size);
            //stack_ = mmap(NULL, conf->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

            cgrp_fd_ = get_cgrp_fd();
        }

        ~task_t()
        {
            //free(stack_);
            //munmap(stack_, task_->stack_size);
            close(cgrp_fd_);
        }

        int run_task()
        {
            pid_t pid = launch_task();
            return wait_for_task(pid);
        }

        int wait_for_task(pid_t pid)
        {
            int stat{};
            pid_t p = waitpid(pid, &stat, 0);

            if (p < 0)
            {
                terminate("waitpid() failed. Stat: {}, RV : {}, Errno: {}", stat, p, errno);
            }
            //logs::debug("Child exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
            return stat;
        }
    private:
        pid_t launch_task()
        {
            return run_task_in_cgroup();
        }

        pid_t run_task_in_cgroup()
        {
            clone_args args = create_clone_args(*task_, stack_, cgrp_fd_);

            auto cargs = convert_to_arg_array(task_->args);
            static char *environ[] = { NULL };

            //pid_t outside_pid = fork();
            //pid_t outside_pid = clone(test, (void*)(args.stack + args.stack_size), SIGCHLD, 0);

            pid_t outside_pid = syscall(SYS_clone3, &args, sizeof(clone_args));
            if (outside_pid < 0)
            {
                terminate("Cannot run process, clone3 failed. Errno: {}", errno);
            }
                
            else if (!outside_pid)
            {
                cpp_execve(task_->executable, task_->args);

                // We should never get here
                terminate("Execve failed. Errno: {}", errno);
            }
            return outside_pid;
        }

        int get_cgrp_fd()
        {
            return open(task_->cgrp_path.c_str(), O_DIRECTORY | O_RDONLY);
        }



        void* stack_;
        config::task_config* task_;
        int cgrp_fd_;

    };

}

namespace process
{
    using namespace tasks;
    namespace cgrp = cgrp_management;
    namespace fs = std::filesystem;

    class root_container_supervisor
    {
        cgrp::root_cgroup_manager cgrp_mngr;
    public:
        root_container_supervisor(const config::main_config& config_struct) : cgrp_mngr(config_struct._cgrp)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        int run_directly()
        {
            return 0;
        }

        int run_with_proxy_process()
        {
            return 0;
        }

    private:
    };

    class proxy_task_batch_supervisor
    {
        config::proxy_config* _conf;

    public:
        proxy_task_batch_supervisor(config::proxy_config& conf) {}

        int run_tasks()
        {
            for(auto&& task : _conf->tasks)
            {
                tasks::task_t task_(task.get());
                std::cout << task_.run_task() << '\n'; 
            }
            return 0;
        }

        

        void collect_results()
        {

        }

    private:
        

        

        
    };

    static int proxy_process(void* proxy_config_ptr)
    {
        config::proxy_config* conf(static_cast<config::proxy_config*>(proxy_config_ptr));
        proxy_task_batch_supervisor supervisor(*conf);
        supervisor.run_tasks();
        supervisor.collect_results();

        return 0;
    }

    static int child_process(void* arg)
    {
        return 0;
    }

}

#endif