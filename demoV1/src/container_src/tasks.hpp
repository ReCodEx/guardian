#ifndef TASKS
#define TASKS

#include <filesystem>

#include <sys/wait.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>

namespace tasks
{
    using namespace process_utils;
    namespace fs = std::filesystem;

    class task_t
    {
    public:
        task_t(config::task_config* conf) : task_conf_(conf) 
        {
            //stack_ = std::aligned_alloc(task_->stack_size, task_->stack_size);
            //stack_ = mmap(NULL, conf->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

            cgrp_fd_ = open(task_conf_->cgrp_path.c_str(), O_DIRECTORY | O_RDONLY);
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
            logs::debug("Child exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);

            return stat;
        }
    private:
        pid_t launch_task()
        {
            return run_task_in_cgroup();
        }

        pid_t run_task_in_cgroup()
        {
            logs::debug("Calling clone3 for \"{}\"", task_conf_->executable.string());
            pid_t outside_pid = clone3_task(*task_conf_, stack_, cgrp_fd_);

            if (outside_pid < 0)
            {
                terminate("Cannot run process, clone3 failed. Errno: {}", errno);
            }
                
            else if (!outside_pid)
            {
                set_resource_limits(); //Possible alternative is to set these from the parent process with prlimit() and use cgroup freezer.

                cpp_execve(task_conf_->executable, task_conf_->args);

                // We should never get here
                terminate("Execve failed. Errno: {}", errno);
            }
            return outside_pid;
        }

        int get_cgrp_fd()
        {
            return open(task_conf_->cgrp_path.c_str(), O_DIRECTORY | O_RDONLY);
        }

        void set_resource_limits()
        {
            auto& limits = task_conf_->rlims;
            
            //std::cout << std::format("Setting memory limit to {} bytes and cpu time limit to {} seconds.", limits.memory_bytes, limits.cpu_time_s) << std::endl;
            set_mem_limit(limits.memory_bytes);
            set_cpu_limit(limits.cpu_time_s);
        }

        void set_mem_limit(unsigned int bytes)
        {
            rlimit mem{bytes,bytes};
            if(setrlimit(RLIMIT_AS, &mem));
            {
                std::cout << std::format("Failed to set memory limit for the child process. Errno: {}", errno);
            }
        }

        void set_cpu_limit(unsigned int s)
        {
            rlimit cpu_time{s,s};
            if(setrlimit(RLIMIT_CPU, &cpu_time))
            {
                std::cout << std::format("Failed to set cpu_time limit for the child process. Errno: {}", errno);
            }
        }

        void* stack_;
        config::task_config* task_conf_;
        int cgrp_fd_;
    };


}


#endif