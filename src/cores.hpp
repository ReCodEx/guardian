#ifndef CORES
#define CORES

#include <fcntl.h>
#include <linux/sched.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "cgrps.hpp"
#include "cli_options.hpp"
#include "config.hpp"
#include "credentials.hpp"
#include "environment.hpp"
#include "lock.hpp"
#include "logs.hpp"
#include "meta_file.hpp"
#include "meta_sink.hpp"
#include "quota.hpp"
#include "tasks.hpp"
#include "terminate.hpp"
#include "utils.hpp"

namespace cores {
    using namespace tasks;
    namespace fs = std::filesystem;

    /// @brief Core class implementing responsibilities of the proxy process,
    /// see TODO: link
    class proxy_core {
       public:
        /// @brief
        /// @param config Proxy node of the configuration.
        /// @param root_creds Called to retrieve assigned credentials.
        /// @param meta_write_fd Compat-mode result channel (ADR 0005, C1): the
        /// write end of the pipe to the root process. `-1` in standalone mode,
        /// where the task writes its own stats-yaml and this exit code is
        /// ignored — it is a capability, not a mode flag.
        proxy_core(const config::proxy_config& config,
                   credentials::root_credentials_manager& root_creds,
                   int meta_write_fd = -1)
            : proxy_config_(&config),
              credentials_mngr_(root_creds),
              mount_mngr_(config, credentials_mngr_),
              fs_manager_(config.fs_config(), credentials_mngr_),
              env_manager_(config.get_env_config()),
              cg_mngr_(),
              task_runner_(config.get_tasks_config(), credentials_mngr_,
                           env_manager_),
              meta_write_fd_(meta_write_fd) {
            logs::info("Hello world from the proxy!");
            init_proxy_logger();
        }

        /// @brief Run the proxy process and the tasks, then always `exit(0)`.
        /// @details When a meta pipe was supplied (compat `--run`), the single
        /// task's `task_stats` is pushed up the pipe; the root process reads it
        /// and owns the 0/1/2 exit-code decision (ADR 0005, C1). Standalone
        /// consumers read the per-task stats-yaml the task itself wrote, and
        /// ignore both the pipe (absent) and this exit code.
        [[noreturn]] void run() {
            proxy_env_setup();
            auto task_report = task_runner_.run_all_tasks();
            generate_proxy_report(task_report);

            if (meta_write_fd_ >= 0) {
                // Compat `--run` builds exactly one task from the positional
                // program; the meta pipe carries that one result.
                const auto& tasks = task_report.tasks();
                if (tasks.size() != 1) {
                    terminate("Compat --run expected 1 task, got {}",
                              tasks.size());
                }
                meta::write_record(meta_write_fd_, tasks.front());
            }
            exit(0);
        }

       private:
        /// @brief
        const config::proxy_config* proxy_config_;

        /// @brief Responsible for switching credentials to the values assigned
        /// to the box.
        credentials::proxy_credentials_manager credentials_mngr_;

        /// @brief Responsible for preparing the mount namespace.
        env::proxy_mount_manager mount_mngr_;

        /// @brief Responsible for creating the box directory tree.
        env::box_fs_manager fs_manager_;

        /// @brief Responsible for generating a set of environment variables for
        /// the task processes.
        env::env_manager env_manager_;

        /// @brief Responsible for setting up the cgroup tree.
        cgroup::proxy_cgroup_manager cg_mngr_;

        /// @brief Responsible for launching and evaluating tasks.
        tasks::task_manager task_runner_;

        /// @brief Write end of the compat-mode meta pipe, or `-1` (standalone).
        int meta_write_fd_;

        void init_proxy_logger() {}

        /// @brief Setup the environment for the tasks.
        void proxy_env_setup() {
            init_proxy_logger();

            /// Run the mount manager first, both the fs_manager and cg_manager
            /// are dependent on correct setup.
            mount_mngr_.run();

            fs_manager_.run();
            pivot_root();
            /// The cgroup manager is intended to run after changing root.
            cg_mngr_.run();
        }

        void generate_proxy_report(const config::task_report& task_report) {}

        /// @brief Change root to the root of box directory tree.
        void pivot_root() {
            auto& box_root = credentials_mngr_.box_root();
            chdir(box_root.c_str());
            if (syscall(SYS_pivot_root, ".", ".")) {
                terminate("pivot_root() failed, errno: {}", errno);
            }

            /// MNT_DETACH ensures we don't get a busy error.
            if (umount2(".", MNT_DETACH)) {
                terminate("umount() on old root failed, errno: {}", errno);
            }
        }
    };

    /// @brief Manager class responsible for launching the proxy process and
    /// waiting for its exit.
    class proxy_connector {
       public:
        proxy_connector() = default;

        proxy_connector(config::root_configuration& root_config,
                        credentials::root_credentials_manager& credentials,
                        cgroup::root_cgroup_manager& cg_manager)
            : root_config_(&root_config),
              credentials_(&credentials),
              cg_mngr_(&cg_manager) {}

        /// @brief Run the proxy process and wait for its exit (standalone).
        /// @return The proxy's raw waitpid() status. Standalone ignores it (the
        /// task wrote its own stats-yaml); no meta pipe is opened.
        int run_and_wait_for_proxy() {
            pid_t proxy_pid = spawn_proxy();
            return wait_for_proxy(proxy_pid);
        }

        /// @brief Run the proxy and collect the single task's result over the
        /// meta pipe (compat `--run`, ADR 0005 C1).
        /// @return The record read outcome: a complete `task_stats`, or
        /// empty/partial when the proxy died before/mid-report — which root
        /// turns into the Guardian's exit-code contract (0/1 vs 2).
        /// @details Opens a `pipe2(O_CLOEXEC)` before `clone3` so the cloned
        /// proxy inherits the write end and the task's `execve` auto-closes it
        /// (a leaked write fd would defeat the EOF-means-failure signal *and*
        /// hand untrusted code an fd into root's pipe). Root is not a writer,
        /// so it closes the write end right after `clone3`; the proxy is then
        /// the sole writer and its exit produces EOF at root's read end.
        meta::read_result run_and_collect_meta() {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) < 0) {
                terminate("Failed to open meta pipe, errno: {}", errno);
            }

            meta_write_fd_ = fds[1];
            pid_t proxy_pid = spawn_proxy();
            close(fds[1]);        // root never writes the meta pipe
            meta_write_fd_ = -1;  // reset (only valid across the clone3)

            int stat = wait_for_proxy(proxy_pid);
            meta::read_result result = meta::read_record(fds[0]);
            close(fds[0]);

            logs::debug("Proxy waitpid stat {}, meta read status {}", stat,
                        static_cast<int>(result.status));
            return result;
        }

       private:
        /// @brief Write end of the compat meta pipe, handed to the cloned proxy
        /// via `spawn_proxy`; `-1` outside a `--run` (standalone).
        int meta_write_fd_ = -1;

        /// @brief Internal representation of the instance configuration.
        config::root_configuration* root_config_;

        /// @brief Responsible for assigning credentials (box_id, UID/GID) used
        /// by the box.
        credentials::root_credentials_manager* credentials_;

        /// @brief Responsible for setting up for the root level of the cgroup
        /// hierarchy.
        cgroup::root_cgroup_manager* cg_mngr_;

        /// @brief Run the proxy process.
        /// @return PID of the proxy as returned by clone3().
        pid_t spawn_proxy() {
            auto& proxy_conf = root_config_->get_proxy_config();
            logs::debug("Calling clone3 for the proxy process");
            pid_t outside_pid =
                clone3_proxy(proxy_conf, cg_mngr_->open_proxy_fd());

            if (outside_pid < 0) {
                terminate(
                    "Cannot run the proxy process, clone3 failed. Errno: {}",
                    errno);
            }

            else if (!outside_pid) {
                // we are in the proxy process
                // Disable the meta-sink first thing: only root writes the host
                // meta-file. A proxy-side terminate() from here on just logs +
                // exit(2), which root reads as an empty pipe -> status:XX (ADR
                // 0005 C2).
                meta::disarm_sink();
                cg_mngr_->close_proxy_fd();
                proxy_core proxy(proxy_conf, *credentials_, meta_write_fd_);
                proxy.run();

                // We will never get here
                terminate("Something very weird happened");
            }
            cg_mngr_->close_proxy_fd();
            return outside_pid;
        }

        /// @brief Wait for the proxy using waitpid().
        /// @param proxy_pid PID of the proxy as returned by spawn_proxy().
        /// @return The proxy's raw waitpid() status.
        static int wait_for_proxy(pid_t proxy_pid) {
            int stat{};
            auto p = waitpid(proxy_pid, &stat, 0);

            if (p != proxy_pid) {
                terminate(
                    "waitpid() for the proxy process failed. Stat: {}, Errno: "
                    "{}",
                    stat, errno);
            }

            logs::debug("Proxy exited. Signal: {}, RV : {}, Errno: {}",
                        WTERMSIG(stat), p, errno);
            return stat;
        }

        /// @brief Generate clone_args struct for cloning the proxy process.
        /// @param proxy_conf Proxy configuration node.
        /// @param cgrp_fd FD of cgroup to launch proxy in.
        /// @return
        clone_args proxy_clone_args(const config::proxy_config& proxy_conf,
                                    uint64_t cgrp_fd) {
            clone_args args{.flags = 0};
            args.exit_signal = SIGCHLD;

            args.flags = CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWPID |
                         CLONE_NEWCGROUP | CLONE_NEWUTS | CLONE_INTO_CGROUP;
            if (!proxy_conf.share_net()) {
                args.flags |= CLONE_NEWNET;
            }
            args.cgroup = cgrp_fd;
            return args;
        }

        /// @brief Launch the proxy process with clone3().
        /// @param config Proxy configuration node.
        /// @param cgrp_fd FD of the cgroup to launch proxy in.
        /// @return PID of the proxy process.
        pid_t clone3_proxy(const config::proxy_config& config,
                           uint64_t cgrp_fd) {
            auto args = proxy_clone_args(config, cgrp_fd);
            return syscall(SYS_clone3, &args, sizeof(clone_args));
        }
    };

    /// @brief Core class implementing responsibilities of the root process.
    /// TODO: link
    class root_core {
       public:
        root_core(int argc, char** argv)
            : root_config_(argc, argv),
              credentials_(root_config_.get_credentials_config()),
              cg_mngr_(root_config_, credentials_),
              proxy_connector_(root_config_, credentials_, cg_mngr_)

        {
            logs::info("Hello world from the Guardian!");
        }

        /// @brief The "main" function of an instance. Dispatches to the phase
        /// selected on the command line: the single-shot standalone flow, or
        /// one of the three isolate-compatibility phases. The enum
        /// is owned by root_configuration (the parser); the switch lives here
        /// so the phases stay methods on root_core and main() need not see the
        /// mode.
        void run() {
            // Fail-fast privilege gate before touching any on-disk state (meta
            // file, locks, box tree): a non-root invocation dies here, and the
            // 4755 install's stale caller egid is fixed to root (ADR 0002).
            // Runs before arm_sink() so a non-root launch fails meta-less.
            credentials::require_root();

            // Arm the internal-error meta-sink for any compat phase given
            // --meta, so a root-side terminate() leaves a status:XX meta (ADR
            // 0005 C2). meta() is unset in standalone, so it stays disarmed
            // there (and until this point, so parse-time failures are
            // meta-less).
            if (root_config_.meta()) {
                meta::arm_sink(*root_config_.meta());
            }

            switch (root_config_.mode()) {
                case cli::run_mode::standalone:
                    run_standalone();
                    break;
                case cli::run_mode::init:
                    init();
                    break;
                case cli::run_mode::run:
                    run_command();
                    break;
                case cli::run_mode::cleanup:
                    cleanup();
                    break;
                case cli::run_mode::none:
                    // cli::parse() already exits 2 on a missing mode, so this
                    // is unreachable; guard it rather than fall through
                    // silently.
                    terminate("No lifecycle mode selected");
            }
        }

       private:
        /// @brief Internal representation of the instance configuration.
        config::root_configuration root_config_;

        /// @brief Responsible for assigning credentials (box_id, UID/GID) used
        /// by the box.
        credentials::root_credentials_manager credentials_;

        /// @brief Responsible for setting up for the root level of the cgroup
        /// hierarchy.
        cgroup::root_cgroup_manager cg_mngr_;

        /// @brief Responsible for launching the proxy process and waiting for
        /// its exit.
        proxy_connector proxy_connector_;

        /// @brief The single-shot YAML flow: derive ids, create the root dir,
        /// set up the cgroup, then launch and wait for the proxy. Unchanged
        /// from the pre-compat monolithic path.
        void run_standalone() {
            credentials_.run();
            create_sandbox_root_dir();
            cg_mngr_.run();
            proxy_connector_.run_and_wait_for_proxy();
        }

        /// @brief `--init`: create the persistent box directory tree keyed by
        /// `--box-id`, set its disk quota, and print its root, so the Worker
        /// can stage job files before a later `--run`. No cgroup, no mounts.
        /// @details Derives the box identity, then sequences the lock around
        /// the build as reset-first / set-last: the `is_initialized` bit is
        /// cleared before any on-disk change and set only once the tree is
        /// complete, so a crashed `--init` leaves the box marked uninitialized.
        /// A box that is already initialized is refused (exit 2) rather than
        /// silently rebuilt (our divergence from upstream Isolate — a double
        /// `--init` without an intervening `--cleanup` is a caller bug); a
        /// stray tree left by a crashed prior `--init` (bit already 0) is wiped
        /// and rebuilt.
        [[noreturn]] void init() {
            credentials_.run();

            lock::box_lock lk(credentials_.box_id());
            if (lk.is_initialized()) {
                terminate("Box {} is already initialized; --cleanup it first",
                          credentials_.box_id());
            }
            // reset-first: the box reads uninitialized until the build
            // completes.
            lk.clear();

            const fs::path& box_root = credentials_.box_root();
            std::error_code ec;
            fs::remove_all(box_root,
                           ec);  // absorb crashed-init debris; else no-op
            if (ec) {
                terminate("Failed to clear stale box root {}: {}",
                          box_root.string(), ec.message());
            }

            init_box_dir();

            // Every --init sets the disk quota: to --quota's caps, or cleared
            // when it is absent, so the box never inherits a cap left against
            // the same box_uid. Before the box root reaches
            // stdout: a quota failure is an --init failure.
            quota::set(box_root, credentials_.box_uid(),
                       root_config_.quota().value_or(quota::limits{}));

            // stdout carries only the box-root path (the Worker appends /box);
            // every log byte goes to stderr.
            std::fputs(box_root.string().c_str(), stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);

            lk.mark_initialized();  // set-last
            exit(0);
        }

        /// @brief `--run`: re-enter an initialized box and run one program
        /// inside it (ADR 0005).
        /// @details Takes the box lock for the whole run (a concurrent phase on
        /// the same box fails on the flock) and refuses a box that was never
        /// `--init`'d (exit 2). Any cgroup left by a crashed previous `--run`
        /// is swept before creating a fresh one — the instance cgroup lives
        /// only within run → cleanup (ADR 0005). The box directory tree is
        /// NOT created here; `--run` re-enters what `--init` made.
        ///
        /// The proxy reports the single task's result over the meta pipe (ADR
        /// 0005 C1); root owns the exit-code decision. A complete record maps
        /// to 0 (task OK) or 1 (task ran, result != OK) and, if `--meta` was
        /// given, an Isolate meta-file; an empty/partial pipe means the proxy
        /// died before reporting — the Guardian's own failure (exit 2 +
        /// `status:XX` meta).
        [[noreturn]] void run_command() {
            credentials_.run();

            lock::box_lock lk(credentials_.box_id());
            if (!lk.is_initialized()) {
                terminate("Box {} was not initialized (--init it first)",
                          credentials_.box_id());
            }

            // A --run given --quota changes the box's disk quota for this run
            // and later ones; without it, the cap --init set stays. Set here in
            // root, not by the task: a failure then reaches the meta as
            // status:XX rather than passing for the task's own exit.
            if (root_config_.quota()) {
                quota::set(credentials_.box_root(), credentials_.box_uid(),
                           *root_config_.quota());
            }

            remove_instance_cgroup();  // stale cgroup from a crashed --run
            cg_mngr_.run();

            // Ownership dance, step 2 (see init_box_dir for step 1): hand box/
            // to the box user so the sandboxed task can read staged inputs and
            // write outputs. The task is not running yet, so there is no live
            // mutator; lchown_tree never follows a symlink regardless.
            const fs::path box_dir = credentials_.box_root() / "box";
            file_utils::lchown_tree(box_dir, credentials_.box_uid(),
                                    credentials_.box_gid());

            meta::read_result result =
                proxy_connector_.run_and_collect_meta();
            const auto& meta_path = root_config_.meta();

            if (result.status == meta::read_status::complete) {
                // Ownership dance, step 3: the task has exited (proxy waited on
                // it), so flip box/ back to the caller — the Worker collects
                // outputs as orig_uid. Skipped on an internal error (below),
                // matching Isolate's `rc < 2` guard; --cleanup removes the box
                // either way. lchown_tree walks a tree the untrusted task
                // wrote, but it is dead now and we never follow its symlinks.
                file_utils::lchown_tree(box_dir, credentials_.orig_uid(),
                                        credentials_.orig_gid());

                if (meta_path) {
                    meta::write_result(*meta_path, result.stats);
                }
                // Authoritative result written: disarm so a later teardown
                // terminate() cannot clobber it with status:XX (ADR 0005 C2,
                // write-once).
                meta::disarm_sink();
                exit(meta::result_exit_code(result.stats));
            }

            // Empty/partial pipe: the proxy terminated before reporting a
            // complete result. Root synthesizes the internal-error meta (the
            // proxy's specific cause went to stderr; a dedicated error pipe is
            // deferred — ADR 0005 C1).
            if (meta_path) {
                meta::write_internal_error(
                    *meta_path, "sandbox terminated before reporting a result");
            }
            meta::disarm_sink();
            exit(2);
        }

        /// @brief `--cleanup`: tear down a box — remove its directory tree and
        /// cgroup, then clear the lock record. Idempotent: a missing
        /// box, cgroup, or lock is success (exit 0).
        /// @details Takes the box lock first, so a `--cleanup` racing a live
        /// `--run` on the same box blocks/fails on the flock (exit 2) rather
        /// than pulling the box out from under it. The lock is `clear()`ed
        /// (truncated, never unlinked), leaving the box readable as
        /// uninitialized so a later `--init` on the same id succeeds.
        [[noreturn]] void cleanup() {
            credentials_.run();
            lock::box_lock lk(credentials_.box_id());

            const fs::path& box_root = credentials_.box_root();
            std::error_code ec;
            fs::remove_all(box_root, ec);  // no-op if already gone
            if (ec) {
                terminate("Failed to remove box root {}: {}", box_root.string(),
                          ec.message());
            }

            remove_instance_cgroup();

            lk.clear();
            exit(0);
        }

        /// @brief Remove this instance's cgroup subtree, if it exists.
        /// @details Absent cgroup ⇒ nothing to do (idempotent). The subtree is
        /// created only by `--run`, so this is a no-op after a bare
        /// `--init`.
        void remove_instance_cgroup() {
            const fs::path& cg = credentials_.instance_cgroup();
            std::error_code ec;
            if (fs::is_directory(cg, ec)) {
                rmdir_cgroup_tree(cg);
            }
        }

        /// @brief Depth-first `rmdir` of a cgroup-v2 subtree.
        /// @details cgroupfs entries are either child cgroups (directories,
        /// removable with `rmdir` once empty of processes and children) or
        /// kernel control files (which cannot be `unlink`ed and disappear with
        /// their cgroup). So we recurse into subdirectories only and `rmdir`
        /// each on the way back up — `fs::remove_all` would fail trying to
        /// delete the control files.
        static void rmdir_cgroup_tree(const fs::path& cg) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(cg, ec)) {
                if (entry.is_directory()) {
                    rmdir_cgroup_tree(entry.path());
                }
            }
            if (::rmdir(cg.c_str()) < 0) {
                terminate("Failed to remove cgroup {}: errno {}", cg.string(),
                          errno);
            }
        }

        /// @brief Create the box directory tree for `--init`: `box_root` (left
        /// root-owned, default perms) and the writable working dir
        /// `box_root/box` (`0700`, owned `orig_uid:orig_gid`).
        /// @details `box/` is handed to the *caller* (orig_uid), not the box
        /// user, so the Worker can stage job files into it before `--run` — the
        /// first step of the ownership dance (see run_command). Mode is `0700`,
        /// not the former world-writable `0777`: the caller owns it outright, so
        /// no world bits are needed, and `--run` flips ownership to the box user
        /// for the task and back to the caller afterwards. This retires the
        /// "top security item" the CONTEXT.md integration constraints flagged.
        void init_box_dir() {
            const fs::path box_dir = credentials_.box_root() / "box";

            std::error_code ec;
            fs::create_directories(box_dir, ec);  // makes box_root and box/
            if (ec) {
                terminate("Failed to create box directory {}: {}",
                          box_dir.string(), ec.message());
            }

            fs::permissions(box_dir, fs::perms::owner_all, ec);  // 0700
            if (ec) {
                terminate("Failed to set permissions on {}: {}",
                          box_dir.string(), ec.message());
            }

            if (::chown(box_dir.c_str(), credentials_.orig_uid(),
                        credentials_.orig_gid()) < 0) {
                terminate("Failed to chown box directory {}: errno {}",
                          box_dir.string(), errno);
            }
        }

        /// @brief Reserve and create root directory for the box.
        /// @note Terminates if the directory already exists.
        void create_sandbox_root_dir() {
            auto dir = credentials_.box_root();
            if (fs::is_directory(dir)) {
                terminate("Directory intended for the sandbox already exists!");
            }

            if (!fs::create_directories(dir)) {
                terminate("Failed to create root sandbox directory");
            }
        }
    };
}  // namespace cores

#endif
