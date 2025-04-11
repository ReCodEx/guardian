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
#include "environment.hpp"

namespace tasks
{
    using namespace process_utils;
    namespace fs = std::filesystem;

    /// @brief Responsible for the execution of a single task.
    class task_supervisor
    {
    public:
        
        /// @brief 
        /// @param conf Configuration node of this task. 
        /// @param credentials Is called to set up credentials of the task process.
        /// @param env_manager Is called to set up environment variables of the task process.
        task_supervisor(config::task_config& conf, credentials::proxy_credentials_manager& credentials, env::env_manager& env_manager) :   
        task_config_(&conf),
        credentials_(&credentials),
        env_manager_(&env_manager),
        task_cgrp_(conf.name())
        {}

        /// @brief Prepare and execute the task and return metadata about the execution.
        config::task_stats run_task()
        {
            pid_t pid = launch_task();
            auto stats = wait_for_task(pid);
            task_config_->finalize_task(stats);
            return stats;
        }

    private:
        /// @brief Unused.
        void* stack_ = nullptr;
        
        /// @brief Configuration node of this task.
        config::task_config* const task_config_;
        
        /// @brief Is called to set up credentials of the task process.
        credentials::proxy_credentials_manager* credentials_;
        
        /// @brief Is called to set up environment variables of the task process.
        env::env_manager* env_manager_;
        
        /// @brief Handler of the cgroup created for this task. 
        cgroup::cgroupv2_t task_cgrp_;

        /// @brief Generate metadata about the execution of this task.
        /// @param stat stat_loc output parameter of waitpid() 
        /// @return task_stats struct as defined in the config source file.
        config::task_stats generate_task_stats(const cgroup::cgroupv2_t& task_cgroup, int stat)
        {
            auto r_usage = get_children_rusage();

            return config::task_stats   {
                                .exited_normally = WIFEXITED(stat),
                                .signalled = WIFSIGNALED(stat),
                                .exit_code = WEXITSTATUS(stat),
                                .err_no = errno,
                                .signal = WTERMSIG(stat),

                                .cg_total_mem_bytes = task_cgroup.memory_usage_bytes(),
                                .cg_total_time_usec = task_cgroup.cpu_usage_usec(),

                                .rusage_total_mem_bytes = r_usage.ru_maxrss*1000,
                                .rusage_total_time_usec = rusage_total_time_usec(r_usage),

                                };
        }

        /// @brief Wait for a task process to terminate and return metadata about the execution.
        /// @param task_pid PID of the task process (Returned by clone3()).
        /// @return task_stats struct as defined in the config source file.
        config::task_stats wait_for_task(pid_t task_pid)
        {
            int stat{};
            pid_t p;
            auto stime = std::chrono::system_clock::now();
            auto wall_limit = std::chrono::seconds(task_config_->rlimits().wall_time());

            /// Periodically check if the task has terminated and kill() if it exceeds wall time limit.
            while(true)
            {
                /// WNOHANG flag so that we don't block here.
                p = waitpid(task_pid, &stat, WNOHANG);

                if (p < 0)
                    { terminate("waitpid() for task \"{}\" failed. Stat: {}, Errno: {}", task_config_->name(), stat, errno); }
                else if (p == 0) 
                {
                    auto ctime = std::chrono::system_clock::now();
                    if(ctime - stime < wall_limit)
                    {
                        logs::debug("Task \"{}\" still running after {}s", task_config_->name(), std::chrono::duration_cast<std::chrono::seconds>(ctime - stime).count());
                        std::this_thread::sleep_for(waiting_time());
                    }
                    else
                    {
                        logs::debug("Task \"{}\" killed for exceeding wall time limit", task_config_->name());
                        kill(task_pid, SIGKILL);
                        p = waitpid(task_pid, &stat, 0);
                        return generate_task_stats(task_cgrp_, stat);
                    }
                    
                }
                else
                {
                    logs::debug("Task process exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
                    return generate_task_stats(task_cgrp_, stat);
                }
            }
        }

        /// @brief Clone() the task process, prepare the task-specific part of the environment (like resource limits) and call execve().
        /// @return PID of the task to use in wait_for_task(). Doesn't return in the task process. 
        pid_t launch_task()
        {
            logs::debug("Launching the task process for task \"{}\"", task_config_->name());

            /// TODO: remove usage of FD to get into the cgroup, use add_me() instead.
            auto fd = task_cgrp_.open_fd();
            pid_t clone_rv = clone3_task(stack_, fd);

            if (clone_rv < 0)
                { terminate("clone3() failed for task \"{}\". Errno: {}", task_config_->name(), errno); }
            else if (!clone_rv)
            {
                /// We are in the task process. 
                ///
                /// The steps before call_execve() don't depend on each other, so the order doesn't matter here.

                set_resource_limits(); //Possible (probably stupid) alternative is to set these from the parent process with prlimit() and use cgroup freezer.
                credentials_->switch_to_box();
                optional_chdir();
                redirect_descriptors();
                call_execve();

                /// execve() doesn't return on success.
                terminate("execve() failed for task \"{}\" (executable path: \"{}\"). Errno: {}", task_config_->name(), task_config_->exec_path().string(), errno);
            }
            return clone_rv;
        }

        pid_t clone3_task(void* stack, uint64_t cgrp_fd)
        {
            auto args = task_clone_args(*task_config_, stack, cgrp_fd);
            return syscall(SYS_clone3, &args, sizeof(clone_args));
        }

        /// @brief 
        /// @param task_conf 
        /// @param stack 
        /// @param cgrp_fd 
        /// @return 
        clone_args task_clone_args(const config::task_config& task_conf, void* stack, uint64_t cgrp_fd)
        {
            clone_args args{0};
            args.exit_signal = SIGCHLD;
            args.flags = CLONE_INTO_CGROUP;

            args.cgroup = cgrp_fd;
            return args;
        }

        /// @brief Prepares args and envp arrays and calls execve() on task executable. 
        /// @return execve() return value.
        int call_execve()
        {
            auto& exec = task_config_->exec_path();
            auto cargs = convert_to_argv(task_config_->exec_args());
            char** environ = env_manager_->get_envp().data();
            
            return execve(exec.c_str(), cargs.data(), environ);
        }
        
        /// @brief Redirect stdin, stderr, stdout from/to files if specified in the config.
        void redirect_descriptors()
        {
            auto& stdin_f = task_config_->stdin_file(); 
            if(stdin_f)
            {
                if(!std::freopen(stdin_f.value().c_str(), "r", stdin))
                    { terminate("Couldn't redirect \"{}\" to stdin for task {}", stdin_f.value().string(), task_config_->name()); }
            }
            
            auto& stderr_f = task_config_->stderr_file(); 
            if(stderr_f)
            {
                if(!std::freopen(stderr_f.value().c_str(), "w", stderr))
                    { terminate("Couldn't redirect stderr to {} for task {}", stderr_f.value().string(), task_config_->name()); }
            }
            
            auto& stdout_f = task_config_->stdout_file(); 
            if(stdout_f)
            {
                if(!std::freopen(stdout_f.value().c_str(), "w", stdout))
                    { terminate("Couldn't redirect \"{}\" to stdin for task {}", stdout_f.value().string(), task_config_->name()); }
            }
            
            if(task_config_->stderr_to_stdout())
            {
                if(dup2(1, 2) < 0)
                    { terminate("dup2() failed while redirecting stderr to stdout."); }
            }
        }
        
        /// @brief Change directory before execve() if specified in the config.
        void optional_chdir()
        {
            if(task_config_->chdir())
            { 
                auto dir = task_config_->chdir().value(); 
                logs::debug("Changing directory to {}", dir.string());
                if(chdir(dir.c_str()))
                    { terminate("chdir() to {} inside box failed.", dir.string()); }
            }
        }
        
        /// @brief Set resource limits for the task process, as specified in the task config node.
        void set_resource_limits()
        {
            auto& limits = task_config_->rlimits();

            if(limits.memory()) set_mem_limit(limits.memory().value());

            if(limits.cpu_time()) set_cpu_limit(limits.cpu_time().value());

            if(limits.as_size()) set_as_size_limit(limits.as_size().value());

            if(limits.processes()) set_processes_limit(limits.processes().value());

            if(limits.disk_usage()) set_disk_quota_quotactl(limits.disk_usage().value());
        }

        /// @brief Set the limit on memory utilization for the task.
        void set_mem_limit(size_t bytes)
        {
            task_cgrp_.set_strict_memory_limit(bytes);
        }

        /// @brief Set the limit on total disk utilization. Will work only on filesystems supporting quotactl() (i.e. not btrfs).
        /// @param bytes The limit in bytes.
        /// TODO: add inodes limit?
        void set_disk_quota_quotactl(size_t bytes)
        {
            /// TODO: comments
            std::string device = devices::find_device_for_dir(fs::path("."));
            uid_t box_uid = credentials_->box_uid();
            std::cout << device << std::endl;
            std::cout << box_uid << std::endl;
            struct dqblk dq = 
            {
                .dqb_bhardlimit = bytes / 1024,
                .dqb_bsoftlimit = bytes / 1024,
                .dqb_ihardlimit = 10,
                .dqb_isoftlimit = 10,
                .dqb_valid = QIF_LIMITS,
                //.dqb_valid = QIF_BLIMITS,
            };
            if(quotactl(QCMD(Q_SETQUOTA, USRQUOTA), device.c_str(), box_uid, (caddr_t) &dq) < 0)
                { terminate("quotactl() failed, errno: {}", errno); }
        }

        
        void set_processes_limit(size_t n)
        {
            task_cgrp_.set_processes_limit(n);
        }

        static void set_as_size_limit(size_t bytes)
        {
            rlimit as{bytes,bytes};
            if(setrlimit(RLIMIT_AS, &as) == -1)
                { terminate("Failed to set address space limit for the child process. Arg: {} Errno: {}", bytes, errno); }
        }

        void set_cpu_limit(size_t s)
        {
            rlimit cpu_time{s,s};
            if(setrlimit(RLIMIT_CPU, &cpu_time) == -1)
                { terminate("setrlimit() failed when setting cpu_time limit for task {}. Errno: {}", task_config_->name(), errno); }
        }

        std::chrono::milliseconds waiting_time()
        {
            return std::chrono::milliseconds(1000);
        }

        rusage get_children_rusage()
        {
            rusage r_usage;
            if(getrusage(RUSAGE_CHILDREN, &r_usage))
                { logs::error("getrusage() failed"); }
            return r_usage;
        }

        /// @brief Get total CPU time used by the process from rusage structure.
        /// @param r_usage 
        /// @return CPU time in microseconds.
        long rusage_total_time_usec(const rusage& r_usage)
        {
            return (r_usage.ru_utime.tv_sec + r_usage.ru_stime.tv_sec)*1000000 + r_usage.ru_utime.tv_usec + r_usage.ru_utime.tv_usec;
        }
    };
    
    class task_manager
    {
    public:
        task_manager(const config::tasks_config& tasks, credentials::proxy_credentials_manager& credentials, env::env_manager& env) :  
        tasks_config(&tasks),
        credentials_manager_(&credentials),
        env_manager_(&env)
        {}

        config::task_report run_all_tasks()
        {
            config::task_report report;

            for(auto&& task_config : tasks_config->get_tasks())
            {
                tasks::task_supervisor task_(*task_config, *credentials_manager_, *env_manager_);
                auto stats = task_.run_task();
                report.insert(stats);
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.cg_total_time_usec, stats.cg_total_mem_bytes);
            }
            return report;
        }
    private:
        /// @brief Configuration node for tasks. 
        const config::tasks_config* tasks_config;
        
        /// @brief Credentials manager class to switch credentials to box values.
        credentials::proxy_credentials_manager* credentials_manager_;
        
        /// @brief Environment manager class which generates envp parameter for execve().
        env::env_manager* env_manager_; 
    };
}


#endif