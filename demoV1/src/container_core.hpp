#ifndef CONTAINER_CORE
#define CONTAINER_CORE

#include "config.hpp"
#include "process.hpp"
#include "tasks.hpp"
#include "environment.hpp"

#include <chrono>
#include <filesystem>


namespace container_core
{
    using namespace tasks;
    namespace fs = std::filesystem;

    class proxy_core
    {
    public:
        proxy_core(config::proxy_config& config) :  proxy_config_(&config),
                                                    mount_mngr_(&config), 
                                                    task_runner_(config.get_tasks_config()),
                                                    fs_manager_(config.fs_config())
        {
            logs::info("Hello world from the proxy!");
            init_proxy_logger();
        }
        
        void run()
        {
            proxy_env_setup();
            auto task_report = task_runner_.run_all_tasks();
            generate_proxy_report(task_report);
            exit(0);
        }

    private: 
        config::proxy_config* proxy_config_;
        env::proxy_mount_manager mount_mngr_;
        env::box_fs_manager fs_manager_;
        cgroup::proxy_cgroup_manager cg_mngr_;
        tasks::task_manager task_runner_;

        void init_proxy_logger()
        {

        }

        void proxy_env_setup()
        {
            init_proxy_logger();
            mount_mngr_.run();
            fs_manager_.run();
            pivot_root();
            cg_mngr_.run();
        }

        void generate_proxy_report(const config::task_report& task_report)
        {

        }
        
        void pivot_root()
        {
            if(proxy_config_->box_root())
            {
                auto box_root = proxy_config_->box_root().value();
                process_utils::pivot_root(box_root, box_root / fs::path("old_root"));
            }
        } 
    };

    class root_core
    {
    public:
        root_core(int argc, char** argv) : root_intfc_(argc, argv)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        void run()
        {
            setup();
            pid_t proxy_pid = spawn_and_run_proxy();
            wait_for_proxy(proxy_pid);
            generate_results();
        }

    private:
        config::root_interface root_intfc_;
        cgroup::root_cgroup_manager cg_mngr_;
        
        void setup()
        {
            cg_mngr_.run();
        }

        pid_t spawn_and_run_proxy()
        {
            auto& proxy_conf = root_intfc_.get_proxy_config();
            logs::debug("Calling clone3 for the proxy process");
            pid_t outside_pid = clone3_proxy(proxy_conf, nullptr, cg_mngr_.open_proxy_fd());

            if (outside_pid < 0)
            {
                terminate("Cannot run the proxy process, clone3 failed. Errno: {}", errno);
            }
                
            else if (!outside_pid)
            {
                //we are in the proxy process
                cg_mngr_.close_proxy_fd();
                proxy_core proxy(proxy_conf);
                proxy.run();

                // We will never get here
                terminate("Something very weird happened");
            }
            cg_mngr_.close_proxy_fd();
            return outside_pid;
        }
        
        void wait_for_proxy(pid_t proxy_pid)
        {
            int stat{};
            auto p = waitpid(proxy_pid, &stat, 0);

            if (p != proxy_pid)
                { terminate("waitpid() for the proxy process failed. Stat: {}, Errno: {}", stat, errno); }
            
            logs::debug("Proxy exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
        }
        
        void generate_results()
        {
            root_intfc_.generate_results();
        }
    };



}

#endif