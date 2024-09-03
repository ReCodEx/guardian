#ifndef TASKS
#define TASKS

#include <filesystem>

#include <sys/wait.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "terminate.hpp"
#include "cgrps.hpp"

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

            cgrp_fd_ = get_cgrp_fd();
        }

        ~task_t()
        {
            //free(stack_);
            //munmap(stack_, task_->stack_size);
            close(cgrp_fd_);
        }

        config::task_stats run_task()
        {
            pid_t pid = launch_task();

            auto stats = wait_for_task(pid);
            gen_report_file(stats);
            return stats;
        }

        config::task_stats wait_for_task(pid_t pid)
        {
            int stat{};
            pid_t p = waitpid(pid, &stat, 0);

            if (p < 0)
            {
                terminate("waitpid() failed. Stat: {}, RV : {}, Errno: {}", stat, p, errno);
            }
            logs::debug("Child exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);

            return config::task_stats   {
                                .exit_code = WIFEXITED(stat),
                                .err_no = errno,
                                .signal = WTERMSIG(stat),

                                .total_mem_bytes = cgroup::memory_usage_bytes(task_conf_->cg_rel_path()),
                                .total_time_usec = cgroup::cpu_usage_usec(task_conf_->cg_rel_path())
                                        };
        }

    private:

        pid_t launch_task()
        {
            return run_task_in_cgroup();
        }

        pid_t run_task_in_cgroup()
        {
            logs::debug("Calling clone3 for \"{}\"", task_conf_->exec_path().string());
            pid_t outside_pid = clone3_task(*task_conf_, stack_, cgrp_fd_);

            if (outside_pid < 0)
            {
                terminate("Cannot run process, clone3 failed. Errno: {}", errno);
            }
                
            else if (!outside_pid)
            {
                set_resource_limits(); //Possible alternative is to set these from the parent process with prlimit() and use for example cgroup freezer.

                cpp_execve(task_conf_->exec_path(), task_conf_->exec_args());

                // We should never get here
                terminate("Execve failed. Errno: {}", errno);
            }
            return outside_pid;
        }

        int get_cgrp_fd()
        {
            fs::path cg_path(cgroup::cg_abs_path(task_conf_->cg_rel_path()));
            return open(cg_path.c_str(), O_DIRECTORY | O_RDONLY);
        }

        void set_resource_limits()
        {
            auto& limits = task_conf_->rlims();
            
            //std::cout << std::format("Setting memory limit to {} bytes and cpu time limit to {} seconds.", limits.memory_bytes, limits.cpu_time_s) << std::endl;
            if(limits.memory())
            {
                set_mem_limit(limits.memory().value());
            }
            if(limits.cpu_time())
            {
                set_cpu_limit(limits.cpu_time().value());
            }
        }

        static void set_mem_limit(unsigned int bytes)
        {
            rlimit mem{bytes,bytes};
            if(setrlimit(RLIMIT_AS, &mem) == -1)
            {
                std::cout << std::format("Failed to set memory limit for the child process. Arg: {} Errno: {}", bytes, errno);
            }
        }

        static void set_cpu_limit(unsigned int s)
        {
            rlimit cpu_time{s,s};
            if(setrlimit(RLIMIT_CPU, &cpu_time) == -1)
            {
                std::cout << std::format("Failed to set cpu_time limit for the child process. Errno: {}", errno);
            }
        }

        /*
        */
        void gen_report_file(const config::task_stats& stats)
        {
            std::ofstream f("/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/logs/task_report.txt");
            if(stats.signal)
            {
                f << "KILLED";
            }
            else
            {
                f << "OK";
            }
        }

        void* stack_ = nullptr;
        config::task_config* const task_conf_;
        int cgrp_fd_;
    };


}


#endif