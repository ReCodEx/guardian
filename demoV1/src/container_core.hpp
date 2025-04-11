#ifndef CONTAINER_CORE
#define CONTAINER_CORE

#include "config.hpp"
#include "process.hpp"
#include "tasks.hpp"
#include "environment.hpp"
#include "credentials.hpp"

#include <chrono>
#include <filesystem>


namespace container_core
{
    using namespace tasks;
    namespace fs = std::filesystem;

    /// @brief Implementation of responsibilities of the proxy process, see TODO: link
    class proxy_core
    {
    public:
        proxy_core(const config::proxy_config& config, credentials::root_credentials_manager& root_creds) :  proxy_config_(&config),
                                                    credentials_mngr_(root_creds),
                                                    mount_mngr_(config, credentials_mngr_), 
                                                    fs_manager_(config.fs_config(), credentials_mngr_),
                                                    env_manager_(config.get_env_config()),
                                                    cg_mngr_(),
                                                    task_runner_(config.get_tasks_config(), credentials_mngr_, env_manager_)
        {
            logs::info("Hello world from the proxy!");
            init_proxy_logger();
        }
        
        /// @brief Run the proxy process and the tasks.
        void run()
        {
            proxy_env_setup();
            auto task_report = task_runner_.run_all_tasks();
            generate_proxy_report(task_report);
            exit(0);
        }

    private: 
        /// @brief 
        const config::proxy_config* proxy_config_;
        
        /// @brief Responsible for switching credentials to the values assigned to the box.
        credentials::proxy_credentials_manager credentials_mngr_;
        
        /// @brief Responsible for preparing the mount namespace.
        env::proxy_mount_manager mount_mngr_;
        
        /// @brief Responsible for creating the box directory tree.
        env::box_fs_manager fs_manager_;
        
        /// @brief Responsible for generating a set of environment variables for the task processes.
        env::env_manager env_manager_;

        /// @brief Responsible for setting up the cgroup tree.
        cgroup::proxy_cgroup_manager cg_mngr_;
        
        /// @brief Responsible for launching and evaluating tasks.
        tasks::task_manager task_runner_;

        void init_proxy_logger()
        {

        }

        /// @brief Setup the environment for the tasks.
        void proxy_env_setup()
        {
            init_proxy_logger();
            
            /// Run the mount manager first, both the fs_manager and cg_manager are dependent.
            mount_mngr_.run();
            fs_manager_.run();

            pivot_root();
            /// The cgroup manager is implemented to run in the new cgroup namespace and after changing root.
            cg_mngr_.run();
        }

        void generate_proxy_report(const config::task_report& task_report)
        {

        }
        
        /// @brief Change root to the root of box directory tree.
        void pivot_root()
        {
            auto& box_root = credentials_mngr_.box_root();
            auto put_old = box_root / fs::path("old_root");

            if(!fs::is_directory(put_old))
            { 
                logs::debug("Creating directory: {}", put_old.string());

                if(!fs::create_directories(put_old))
                    { terminate("Failed to create put_old directory for pivot_root"); } 
            } 

            if(syscall(SYS_pivot_root, box_root.c_str(), put_old.c_str()))
                { terminate("pivot_root failed, errno: {}", errno); }
            chdir("/");
            
            // !!!
            /// MNT_DETACH to get around busy error.
            if(umount2("/old_root", MNT_DETACH))
                { logs::error("umount on old root failed, errno: {}", errno); }
        } 
    };

    /// @brief Implements the responsibilities of the root process. 
    class root_core
    {
    public:
        root_core(int argc, char** argv) : root_config_(argc, argv), credentials_(), cg_mngr_(root_config_, credentials_)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        /// @brief The "main" function.
        void run()
        {
            setup();
            pid_t proxy_pid = spawn_proxy();
            wait_for_proxy(proxy_pid);
            generate_results();
        }

    private:
        /// @brief Internal representation of container configuration.
        config::root_configuration root_config_;

        /// @brief Responsible for assigning credentials (box_id, UID/GID) used by the box.
        credentials::root_credentials_manager credentials_;

        /// @brief Responsible for setting up for the root level of the cgroup hierarchy.
        cgroup::root_cgroup_manager cg_mngr_;
        
        /// @brief Reserve and prepare identifiers ( box ID, ...) and global resources ( root directory, cgroup, ...)
        void setup()
        {
            credentials_.run();
            create_box_dir();
            cg_mngr_.run();
        }

        /// @brief Clone the proxy process in new namespaces and assigned cgroup. 
        /// @return PID of the proxy returned by clone()
        pid_t spawn_proxy()
        {
            auto& proxy_conf = root_config_.get_proxy_config();
            logs::debug("Calling clone3 for the proxy process");
            pid_t outside_pid = clone3_proxy(proxy_conf, cg_mngr_.open_proxy_fd());

            if (outside_pid < 0)
                { terminate("Cannot run the proxy process, clone3 failed. Errno: {}", errno); }
                
            else if (!outside_pid)
            {
                //we are in the proxy process
                cg_mngr_.close_proxy_fd();
                proxy_core proxy(proxy_conf, credentials_);
                proxy.run();

                // We will never get here
                terminate("Something very weird happened");
            }
            cg_mngr_.close_proxy_fd();
            return outside_pid;
        }
        
        /// @brief Wait for the proxy using waitpid().
        /// @param proxy_pid PID of the proxy.
        void wait_for_proxy(pid_t proxy_pid)
        {
            int stat{};
            auto p = waitpid(proxy_pid, &stat, 0);

            if (p != proxy_pid)
                { terminate("waitpid() for the proxy process failed. Stat: {}, Errno: {}", stat, errno); }
            
            logs::debug("Proxy exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
        }

        /// @brief Generate clone_args struct for cloning the proxy process.
        /// @param proxy_conf Proxy configuration node.
        /// @param cgrp_fd FD of cgroup to launch proxy in.
        /// @return 
        clone_args proxy_clone_args(const config::proxy_config& proxy_conf, uint64_t cgrp_fd)
        {
            clone_args args{0};
            args.exit_signal = SIGCHLD;
            args.flags = config::DEFAULT_CLONE_FLAGS | CLONE_INTO_CGROUP;

            args.cgroup = cgrp_fd;
            return args; 
        }
        
        void generate_results()
        {
        }
        
        /// @brief Reserve and create root directory for the box.
        /// @note Terminates if the directory already exists.
        void create_box_dir()
        {
            auto dir = credentials_.box_root();
            if(fs::is_directory(dir))
                { terminate("Directory intended for box already exists!"); }
            
            if(!fs::create_directories(dir))
                { terminate("Failed to create box directory"); }
        }

        /// @brief Launch the proxy process with clone3().
        /// @param config Proxy configuration node.
        /// @param cgrp_fd FD of the cgroup to launch proxy in.
        /// @return PID of the proxy process.
        pid_t clone3_proxy(const config::proxy_config& config, uint64_t cgrp_fd)
        {
            auto args = proxy_clone_args(config, cgrp_fd);
            return syscall(SYS_clone3, &args, sizeof(clone_args));
        }
    };



}

#endif