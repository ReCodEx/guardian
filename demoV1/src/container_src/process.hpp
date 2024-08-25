#ifndef PROCESS
#define PROCESS

#include "cgrps.hpp"
#include "config.hpp"
#include "filesystem"
#include "utils.hpp"

#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>

namespace clone_utils
{
    struct clone_args 
    {
        uint64_t flags;        /* Flags bit mask */
        uint64_t pidfd;        /* Where to store PID file descriptor
                            (int *) */
        uint64_t child_tid;    /* Where to store child TID,
                            in child's memory (pid_t *) */
        uint64_t parent_tid;   /* Where to store child TID,
                            in parent's memory (pid_t *) */
        uint64_t exit_signal;  /* Signal to deliver to parent on
                            child termination */
        uint64_t stack;        /* Pointer to lowest byte of stack */
        uint64_t stack_size;   /* Size of stack */
        uint64_t tls;          /* Location of new TLS */
        uint64_t set_tid;      /* Pointer to a pid_t array
                            (since Linux 5.5) */
        uint64_t set_tid_size; /* Number of elements in set_tid
                            (since Linux 5.5) */
        uint64_t cgroup;       /* File descriptor for target cgroup
                            of child (since Linux 5.7) */
    };

    static inline clone_args create_clone_args(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
    {
        clone_args args;
        args.stack = (uint64_t)stack;
        args.stack_size = task_conf.stack_size;
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
}

namespace tasks
{
    using namespace clone_utils;
    namespace fs = std::filesystem;

    class task_t
    {
        void* stack_;
        config::task_config* task_;
    public:
        task_t(config::task_config* conf) : task_(conf) 
        {
            stack_ = std::aligned_alloc(1024, task_->stack_size);
        }

        ~task_t()
        {
            free(stack_);
        }

        int run_task()
        {
            pid_t pid = launch();
            return wait_for_task(pid);
        }
        
        pid_t launch()
        {
            return run_executable_in_cgroup();
        }

        int wait_for_task(pid_t pid)
        {
            int stat;
            pid_t p = waitpid(pid, &stat, 0);
            if (p < 0)
                std::cout << "Proxy waitpid() failed";
            return stat;
        }
    private:
        pid_t run_executable_in_cgroup()
        {
            int cgrp_fd = open(task_->cgrp_path.c_str(), O_DIRECTORY | O_RDONLY);
            clone_args args = create_clone_args(*task_, stack_, cgrp_fd);


            auto cpath = task_->executable.c_str();
            auto cargs = convert_to_arg_array(task_->args);
            static char *environ[] = { NULL };


            pid_t outside_pid = syscall(SYS_clone3, args, sizeof(args));
            if (outside_pid < 0)
                std::cout << "Cannot run process, fork failed\n";
            else if (!outside_pid)
            {
                
                execve(cpath, cargs.data(), environ);
                _exit(42);	// We should never get here
            }

            close(cgrp_fd);
            return outside_pid;
        }
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
        {}

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



    

      

    class proxy_process_supervisor
    {
        config::proxy_config* _conf;

    public:
        proxy_process_supervisor(config::proxy_config& conf) {}

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
        proxy_process_supervisor supervisor(*conf);
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