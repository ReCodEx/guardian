#ifndef TASKS
#define TASKS

#include <fcntl.h>
#include <linux/quota.h>
#include <sys/quota.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#include "cgrps.hpp"
#include "config.hpp"
#include "credentials.hpp"
#include "devices.hpp"
#include "environment.hpp"
#include "logs.hpp"
#include "process.hpp"
#include "terminate.hpp"

namespace tasks {
    using namespace process_utils;
    namespace fs = std::filesystem;

    /// @brief Supervisor class responsible for the execution of a single task.
    class task_supervisor {
       public:
        /// @brief
        /// @param conf Configuration node of this task.
        /// @param credentials Is called to set up credentials of the task
        /// process.
        /// @param env_manager Is called to set up environment variables of the
        /// task process.
        task_supervisor(config::task_config& conf,
                        credentials::proxy_credentials_manager& credentials,
                        env::env_manager& env_manager)
            : task_config_(&conf),
              credentials_(&credentials),
              env_manager_(&env_manager),
              task_cgrp_(conf.name()) {}

        /// @brief Prepare and execute the task and return metadata about the
        /// execution.
        config::task_stats run_task() {
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

        /// @brief Is called to set up environment variables of the task
        /// process.
        env::env_manager* env_manager_;

        /// @brief Handler of the cgroup created for this task.
        cgroup::cgroupv2_t task_cgrp_;

        /// @brief Generate metadata about the execution of this task.
        /// @param stat stat_loc output parameter of waitpid()
        /// @return task_stats struct as defined in the config source file.
        config::task_stats generate_task_stats(
            const cgroup::cgroupv2_t& task_cgroup, int stat,
            size_t wall_time_ms,
            config::exit_status exit = config::exit_status::OK) {
            auto r_usage = get_children_rusage();
            auto peak = task_cgroup.memory_peak_bytes();
            auto memory = peak.value_or(0);
            auto cputime = task_cgroup.cpu_usage_usec();
            bool oom_killed = task_cgroup.oom_killed();

            // Precedence: a timeout verdict (set by wait_for_task on wall
            // overrun) wins over CPU/memory, and CPU wins over memory. Each
            // override only fires when no higher-precedence verdict was already
            // set (guard `exit == OK`), so an incidental later condition can't
            // clobber the real cause of death.
            if (exit == config::exit_status::OK &&
                task_config_->rlimits().cpu_time().has_value() &&
                (float)cputime / 1000000 >
                    task_config_->rlimits().cpu_time().value()) {
                logs::debug(
                    "Task \"{}\" exceeded CPU time limit, cgroup CPU time "
                    "usage: {}, limit: {}",
                    task_config_->name(), (float)cputime / 1000000,
                    task_config_->rlimits().cpu_time().value());
                exit = config::exit_status::CPU_TIME_EXCEEDED;
            }
            // Authoritative memory-limit signal: the kernel OOM-kill counter,
            // not a peak-vs-limit comparison (which can misfire at the limit).
            if (exit == config::exit_status::OK && oom_killed) {
                logs::debug("Task \"{}\" was OOM-killed (memory.events)",
                            task_config_->name());
                exit = config::exit_status::MEMORY_LIMIT_EXCEEDED;
            }

            return config::task_stats{
                .exited_normally = WIFEXITED(stat),
                .signalled = WIFSIGNALED(stat),
                .exit_code = WEXITSTATUS(stat),
                .err_no = errno,
                .signal = WTERMSIG(stat),
                .exit = exit,

                .cg_total_mem_bytes = memory,
                .cg_total_time_usec = cputime,
                .wall_time_ms = wall_time_ms,

                .rusage_max_rss_kb =
                    static_cast<size_t>(r_usage.ru_maxrss),  // already KB
                .rusage_total_time_usec = rusage_total_time_usec(r_usage),

                .csw_voluntary = static_cast<size_t>(r_usage.ru_nvcsw),
                .csw_forced = static_cast<size_t>(r_usage.ru_nivcsw),

                .cg_mem_measured = peak.has_value(),
                .oom_killed = oom_killed,
            };
        }

        /// @brief Wait for a task process to terminate and return metadata
        /// about the execution.
        /// @param task_pid PID of the task process (Returned by clone3()).
        /// @return task_stats struct as defined in the config source file.
        config::task_stats wait_for_task(pid_t task_pid) {
            int stat{};
            pid_t p;
            auto stime = std::chrono::system_clock::now();
            auto wall_limit = std::chrono::duration<double>(
                task_config_->rlimits().wall_time());

            // Periodically check if the task has terminated and kill() if it
            // exceeds wall time limit.
            while (true) {
                // WNOHANG flag so that we don't block here.
                p = waitpid(task_pid, &stat, WNOHANG);
                auto wtime = std::chrono::system_clock::now() - stime;

                if (p < 0) {
                    terminate(
                        "waitpid() for task \"{}\" failed. Stat: {}, Errno: {}",
                        task_config_->name(), stat, errno);
                } else if (p == 0) {
                    if (wtime < wall_limit) {
                        // logs::debug("Task \"{}\" still running after {}s",
                        // task_config_->name(),
                        // std::chrono::duration_cast<std::chrono::seconds>(wtime).count());
                        std::this_thread::sleep_for(waiting_time());
                    } else {
                        logs::debug(
                            "Sending SIGKILL to task \"{}\" for exceeding wall "
                            "time limit",
                            task_config_->name());
                        kill(task_pid, SIGKILL);
                        p = waitpid(task_pid, &stat, 0);
                        logs::debug(
                            "Task \"{}\" exited. WTERMSIG: {}, WEXITSTATUS : "
                            "{}, Errno: {}",
                            task_config_->name(), WTERMSIG(stat),
                            WEXITSTATUS(stat), errno);
                        return generate_task_stats(
                            task_cgrp_, stat,
                            std::chrono::duration_cast<
                                std::chrono::milliseconds>(wtime)
                                .count(),
                            config::exit_status::WALL_TIME_EXCEEDED);
                    }

                } else {
                    logs::debug(
                        "Task \"{}\" exited. WTERMSIG: {}, WEXITSTATUS : {}, "
                        "Errno: {}",
                        task_config_->name(), WTERMSIG(stat), WEXITSTATUS(stat),
                        errno);
                    return generate_task_stats(
                        task_cgrp_, stat,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            wtime)
                            .count());
                }
            }
        }

        /// @brief Clone() the task process, prepare the task-specific part of
        /// the environment (like resource limits) and call execve().
        /// @return PID of the task to use in wait_for_task(). Doesn't return in
        /// the task process.
        pid_t launch_task() {
            logs::debug("Launching the task process for \"{}\"",
                        task_config_->name());

            /// TODO: remove usage of FD to get into the cgroup, use add_me()
            /// instead.
            auto fd = task_cgrp_.open_fd();
            pid_t clone_rv = clone3_task(stack_, fd);

            if (clone_rv < 0) {
                terminate("clone3() failed for task \"{}\". Errno: {}",
                          task_config_->name(), errno);
            } else if (!clone_rv) {
                /// We are in the task process.
                /// Order matches Isolate's box_inside: apply rlimits while
                /// still privileged, drop to the box user, then chdir and open
                /// the redirect files AS THE BOX USER — so `--stdin` respects
                /// box-user permissions and `--stdout`/`--stderr` files are
                /// created box-owned, not root-owned.
                set_resource_limits();
                credentials_->switch_to_box();
                optional_chdir();
                redirect_descriptors();
                call_execve();

                /// execve() doesn't return on success.
                terminate(
                    "execve() failed for task \"{}\" (executable path: "
                    "\"{}\"). Errno: {}",
                    task_config_->name(), task_config_->exec_path().string(),
                    errno);
            }
            return clone_rv;
        }

        /// @brief Prepare clone_args argument for clone3(), based on the task
        /// configuration, and call clone3().
        /// @param stack Currently unused and nullptr is passed.
        /// @param cgrp_fd FD of the directory of this task's cgroup.
        /// @return PID of the task process.
        pid_t clone3_task(void* stack, uint64_t cgrp_fd) {
            auto args = task_clone_args(*task_config_, stack, cgrp_fd);
            return syscall(SYS_clone3, &args, sizeof(clone_args));
        }

        /// @brief Setup the clone_args struct based on the task configuration.
        /// @param task_conf Task configuration node.
        /// @param stack Currently unused.
        /// @param cgrp_fd FD of the directory of this task's cgroup.
        /// @return struct clone_args to pass to clone3.
        clone_args task_clone_args(const config::task_config& task_conf,
                                   void* stack, uint64_t cgrp_fd) {
            clone_args args{0};
            args.exit_signal = SIGCHLD;
            args.flags = CLONE_INTO_CGROUP;

            args.cgroup = cgrp_fd;
            return args;
        }

        /// @brief Prepare argv[] and envp[] arguments with args and env
        /// variables specified in the config, and call execve() for this task.
        /// @return Doesn't return unless execve() fails.
        int call_execve() {
            auto& exec = task_config_->exec_path();
            auto cargs = convert_to_argv(task_config_->exec_args());
            char** environ = env_manager_->get_envp();

            return execve(exec.c_str(), cargs.data(), environ);
        }

        /// @brief Redirect stdin, stderr, stdout from/to files if specified in
        /// the config.
        void redirect_descriptors() {
            auto& stdin_f = task_config_->stdin_file();
            if (stdin_f) {
                if (!std::freopen(stdin_f.value().c_str(), "r", stdin)) {
                    terminate("Couldn't redirect \"{}\" to stdin for task {}",
                              stdin_f.value().string(), task_config_->name());
                }
            }

            auto& stderr_f = task_config_->stderr_file();
            if (stderr_f) {
                if (!std::freopen(stderr_f.value().c_str(), "w", stderr)) {
                    terminate("Couldn't redirect stderr to {} for task {}",
                              stderr_f.value().string(), task_config_->name());
                }
            }

            auto& stdout_f = task_config_->stdout_file();
            if (stdout_f) {
                if (!std::freopen(stdout_f.value().c_str(), "w", stdout)) {
                    terminate("Couldn't redirect \"{}\" to stdout for task {}",
                              stdout_f.value().string(), task_config_->name());
                }
            }

            if (task_config_->stderr_to_stdout()) {
                if (dup2(1, 2) < 0) {
                    terminate(
                        "dup2() failed while redirecting stderr to stdout.");
                }
            }
        }

        /// @brief Change directory before execve() if specified in the config.
        void optional_chdir() {
            if (task_config_->chdir()) {
                auto dir = task_config_->chdir().value();
                logs::debug("Changing directory to {}", dir.string());
                if (chdir(dir.c_str())) {
                    terminate("chdir() to {} inside box failed.", dir.string());
                }
            }
        }

        /// @brief Set resource limits for the task process, as specified in the
        /// task config node.
        void set_resource_limits() {
            auto& rlimits = task_config_->rlimits();

            if (rlimits.cpu_time()) {
                if (rlimits.extra_time()) {
                    set_cpu_time(rlimits.cpu_time().value() +
                                 rlimits.extra_time().value());
                } else {
                    set_cpu_time(rlimits.cpu_time().value());
                }
            }
            if (rlimits.memory()) {
                set_memory_usage(rlimits.memory().value());
            }
            if (rlimits.as_size()) {
                set_as_size(rlimits.as_size().value());
            }
            // Always applied, unlike the other caps: with no stack limit set,
            // Isolate gives the task an *unlimited* stack rather than leaving
            // the caller's in place (isolate/isolate.c:803). The Worker omits
            // --stack whenever its stack-size limit is 0, its default, so this
            // is the usual configuration (#22).
            set_stack_size(rlimits.stack_size().value_or(RLIM_INFINITY));
            if (rlimits.processes()) {
                set_processes_count(rlimits.processes().value());
            }
            if (rlimits.disk_usage()) {
                set_disk_quota_quotactl(rlimits.disk_usage().value());
            }
            if (rlimits.open_files()) {
                set_open_files(rlimits.open_files().value());
            }
            if (rlimits.file_size()) {
                set_file_size(rlimits.file_size().value());
            }
            if (rlimits.core_dump_size()) {
                set_core_dump_size(rlimits.core_dump_size().value());
            }
        }

        /// @brief Set the limit on memory utilization for the task.
        void set_memory_usage(size_t bytes) {
            task_cgrp_.set_strict_memory_limit(bytes);
        }

        /// @brief Set the limit on disk usage (sum of file sizes owned by
        /// box_uid). Works only on filesystems supporting quotactl() (i.e. not
        /// btrfs).
        /// @param bytes The limit in bytes.
        /// TODO: add inodes limit?
        void set_disk_quota_quotactl(size_t bytes) {
            /// TODO: comments
            std::string device = devices::find_device_for_dir(fs::path("."));
            uid_t box_uid = credentials_->box_uid();
            // std::cout << device << std::endl;
            // std::cout << box_uid << std::endl;
            struct dqblk dq = {
                .dqb_bhardlimit = bytes / 1024,
                .dqb_bsoftlimit = bytes / 1024,
                // .dqb_ihardlimit = 10,
                // .dqb_isoftlimit = 10,
                .dqb_valid = QIF_LIMITS,
                //.dqb_valid = QIF_BLIMITS,
            };
            if (quotactl(QCMD(Q_SETQUOTA, USRQUOTA), device.c_str(), box_uid,
                         (caddr_t)&dq) < 0) {
                terminate("quotactl() failed, errno: {}", errno);
            }
        }

        /// @brief Set the limit on the total number of processes the task
        /// launches to 'n'. (e.g. using fork() or commands in a bash script)
        void set_processes_count(size_t n) {
            task_cgrp_.set_processes_limit(n);
        }

        /// @brief Set the limit on address space size of the task. Applies to
        /// each process launched (e.g. using fork() or a command in a bash
        /// script).
        /// @param bytes
        void set_as_size(size_t bytes) {
            rlimit as{bytes, bytes};
            if (setrlimit(RLIMIT_AS, &as) == -1) {
                terminate(
                    "setrlimit() failed when setting address space size limit "
                    "for task \"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        /// @brief Set the limit on stack size of the task. Applies to each
        /// process launched (e.g. using fork() or a command in a bash script).
        /// @param bytes
        void set_stack_size(size_t bytes) {
            rlimit stack{bytes, bytes};
            if (setrlimit(RLIMIT_STACK, &stack) == -1) {
                terminate(
                    "setrlimit() failed when setting stack size limit for task "
                    "\"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        /// @brief Set a limit on CPU time consumed by the task. Applies to each
        /// process launched (e.g. using fork() or a command in a bash script).
        /// @param seconds fractional CPU-second limit. RLIMIT_CPU is
        /// whole-seconds, so we `ceil` it as a coarse kernel backstop; precise
        /// sub-second enforcement comes from the cgroup CPU-time compare.
        void set_cpu_time(double seconds) {
            rlim_t s = static_cast<rlim_t>(std::ceil(seconds));
            rlimit cpu_time{s, s};
            if (setrlimit(RLIMIT_CPU, &cpu_time) == -1) {
                terminate(
                    "setrlimit() failed when setting cpu_time limit for task "
                    "\"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        /// @brief Set a limit on the number of simultaneously opened file
        /// descriptors.
        /// @param bytes
        void set_open_files(size_t n) {
            rlimit files{n, n};
            if (setrlimit(RLIMIT_NOFILE, &files) == -1) {
                terminate(
                    "setrlimit() failed when setting file descriptor limit for "
                    "task \"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        /// @brief Set a limit on maximum file size.
        /// @param bytes
        void set_file_size(size_t bytes) {
            rlimit file{bytes, bytes};
            if (setrlimit(RLIMIT_FSIZE, &file) == -1) {
                terminate(
                    "setrlimit() failed when setting file size limit for task "
                    "\"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        /// @brief Set a limit on maximum size of a core dump when an isolated
        /// process crashes. Longer dumps get truncated to this size.
        /// @param bytes
        void set_core_dump_size(size_t bytes) {
            rlimit core{bytes, bytes};
            if (setrlimit(RLIMIT_CORE, &core) == -1) {
                terminate(
                    "setrlimit() failed when setting core size limit for task "
                    "\"{}\". Errno: {}",
                    task_config_->name(), errno);
            }
        }

        std::chrono::milliseconds waiting_time() {
            return std::chrono::milliseconds(100);
        }

        /// @brief Get the rusage struct with total accounting for all tasks
        /// (all child processes of the proxy process).
        rusage get_children_rusage() {
            rusage r_usage;
            if (getrusage(RUSAGE_CHILDREN, &r_usage)) {
                logs::error("getrusage() failed");
            }
            return r_usage;
        }

        /// @brief Get total CPU time used by the process from rusage structure.
        /// @param r_usage
        /// @return CPU time in microseconds.
        long rusage_total_time_usec(const rusage& r_usage) {
            return (r_usage.ru_utime.tv_sec + r_usage.ru_stime.tv_sec) *
                       1000000 +
                   r_usage.ru_utime.tv_usec + r_usage.ru_stime.tv_usec;
        }
    };

    /// @brief Manager class responsible for running tasks.
    class task_manager {
       public:
        /// @brief
        /// @param tasks Configuration node for tasks.
        /// @param credentials Called to switch credentials to box values.
        /// @param env Called to generate environment variables for a task.
        task_manager(const config::tasks_config& tasks,
                     credentials::proxy_credentials_manager& credentials,
                     env::env_manager& env)
            : tasks_config(&tasks),
              credentials_manager_(&credentials),
              env_manager_(&env) {}

        /// @brief Run all tasks and return metadata about their execution.
        /// @return task_report struct as defined in the configuration source
        /// file.
        config::task_report run_all_tasks() {
            config::task_report report;

            for (auto&& task_config : tasks_config->get_tasks()) {
                tasks::task_supervisor task_(
                    *task_config, *credentials_manager_, *env_manager_);
                auto stats = task_.run_task();
                report.insert(stats);
                logs::debug(
                    "Task finished with exit code: {}, in {} us and {} bytes "
                    "of used memory",
                    stats.exit_code, stats.cg_total_time_usec,
                    stats.cg_total_mem_bytes);
            }
            return report;
        }

       private:
        /// @brief Configuration node for tasks.
        const config::tasks_config* tasks_config;

        /// @brief Called to switch credentials to box values.
        credentials::proxy_credentials_manager* credentials_manager_;

        /// @brief Called to generate environment variables for a task.
        env::env_manager* env_manager_;
    };
}  // namespace tasks

#endif
