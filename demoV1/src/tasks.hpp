#ifndef TASKS
#define TASKS

#include <filesystem>

#include <sys/wait.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/quota.h>

#include "terminate.hpp"
#include "cgrps.hpp"
#include "credentials.hpp"
#include "devices.hpp"

namespace tasks
{
    using namespace process_utils;
    namespace fs = std::filesystem;

    class task_supervisor
    {
    public:
        task_supervisor(config::task_config& conf, credentials::proxy_credentials_manager& credentials) :   task_config_(&conf),
                                                                                                            credentials_(&credentials),
                                                                                                            task_cgrp_(conf.cg_rel_path())
        {}

        config::task_stats run_task()
        {
            pid_t pid = launch_task();
            auto stats = wait_for_task(pid);
            task_config_->finalize_task(stats);
            return stats;
        }

    private:
        void* stack_ = nullptr;
        config::task_config* const task_config_;
        credentials::proxy_credentials_manager* credentials_;
        cgroup::cgroupv2_t task_cgrp_;

        config::task_stats generate_task_stats(int stat)
        {
            auto r_usage = get_children_rusage();

            return config::task_stats   {
                                .exited_normally = WIFEXITED(stat),
                                .signalled = WIFSIGNALED(stat),
                                .exit_code = WEXITSTATUS(stat),
                                .err_no = errno,
                                .signal = WTERMSIG(stat),

                                .cg_total_mem_bytes = task_cgrp_.memory_usage_bytes(),
                                .cg_total_time_usec = task_cgrp_.cpu_usage_usec(),

                                .rusage_total_mem_bytes = r_usage.ru_maxrss*1000,
                                .rusage_total_time_usec = rusage_total_time_usec(r_usage),

                                };
        }

        config::task_stats wait_for_task(pid_t pid)
        {
            int stat{};
            pid_t p;
            auto stime = std::chrono::system_clock::now();
            auto wall_limit = std::chrono::seconds(task_config_->rlimits().wall_time());

            while(true)
            {
                p = waitpid(pid, &stat, WNOHANG);

                if (p < 0)
                {
                    terminate("waitpid() for task \"{}\" failed. Stat: {}, Errno: {}", task_config_->name(), stat, errno);
                }
                else if (p == 0) 
                {
                    auto ctime = std::chrono::system_clock::now();
                    if(ctime - stime < wall_limit)
                    {
                        logs::debug("task still running after {} s", std::chrono::duration_cast<std::chrono::seconds>(ctime - stime).count());
                        std::this_thread::sleep_for(waiting_time());
                    }
                    else
                    {
                        logs::debug("task killed for exceeding wall time limit");
                        kill(pid, SIGKILL);
                        p = waitpid(pid, &stat, 0);
                        break;
                    }
                    
                }
                else break;
            }
            logs::debug("Task process exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
            return generate_task_stats(stat);
        }

        pid_t launch_task()
        {
            return run_task_in_cgroup();
        }

        pid_t run_task_in_cgroup()
        {
            logs::debug("Calling clone3 for \"{}\"", task_config_->exec_path().string());
            auto fd = task_cgrp_.open_fd();

            pid_t outside_pid = clone3_task(*task_config_, stack_, fd);

            if (outside_pid < 0)
            {
                terminate("Cannot run the task process, clone3 failed. Errno: {}", errno);
            }
                
            else if (!outside_pid)
            {
                set_resource_limits(); //Possible alternative is to set these from the parent process with prlimit() and use cgroup freezer.
                set_disk_quota_quotactl(1);
                credentials_->switch_to_box();
                cpp_execve(task_config_->exec_path(), task_config_->exec_args());

                // We should never get here
                terminate("Execve failed. Errno: {}", errno);
            }
            return outside_pid;
        }

        int get_cgrp_fd()
        {
            fs::path cg_path(cgroup::cg_abs_path(task_config_->cg_rel_path()));
            return open(cg_path.c_str(), O_DIRECTORY | O_RDONLY);
        }

        void set_resource_limits()
        {
            auto& limits = task_config_->rlimits();

            if(limits.memory()) set_mem_limit(limits.memory().value());

            if(limits.cpu_time()) set_cpu_limit(limits.cpu_time().value());

            if(limits.as_size()) set_as_size_limit(limits.as_size().value());

            if(limits.processes()) set_processes_limit(limits.processes().value());
        }

        void set_mem_limit(size_t bytes)
        {
            task_cgrp_.set_strict_memory_limit(bytes);
        }

        void set_disk_quota_quotactl(size_t bytes)
        {
            uid_t box_uid = credentials_->box_uid();
            const char* device;
            std::cout << devices::find_cwd_device() << std::endl;
/*             if(quotactl(QCMD(Q_SETQUOTA, USRQUOTA), device, box_uid, (caddr_t) &dq) < 0)
                { terminate("quotactl() failed, errno: {}", errno); } */
        }

        
        void set_processes_limit(size_t n)
        {
            task_cgrp_.set_processes_limit(n);
        }

        static void set_as_size_limit(size_t bytes)
        {
            rlimit as{bytes,bytes};
            if(setrlimit(RLIMIT_AS, &as) == -1)
            {
                std::cerr << std::format("Failed to set address space limit for the child process. Arg: {} Errno: {}", bytes, errno);
            }
        }

        static void set_cpu_limit(size_t s)
        {
            rlimit cpu_time{s,s};
            if(setrlimit(RLIMIT_CPU, &cpu_time) == -1)
            {
                std::cerr << std::format("Failed to set cpu_time limit for the child process. Errno: {}", errno);
            }
        }

        std::chrono::milliseconds waiting_time()
        {
            return std::chrono::milliseconds(1000);
        }

    };
    
    class task_manager
    {
    public:
        task_manager(const config::tasks_config& tasks, credentials::proxy_credentials_manager& credentials) :  tasks_config(&tasks),
                                                                                                                credentials_(&credentials)
        {}

        config::task_report run_all_tasks()
        {
            config::task_report report;

            for(auto&& task_config : tasks_config->get_tasks())
            {
                tasks::task_supervisor task_(*task_config, *credentials_);
                auto stats = task_.run_task();
                report.insert(stats);
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.cg_total_time_usec, stats.cg_total_mem_bytes);
            }
            return report;
        }
    private:
        const config::tasks_config* tasks_config;
        credentials::proxy_credentials_manager* credentials_;
    };
}


#endif