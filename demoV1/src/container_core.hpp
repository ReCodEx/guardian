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

        proxy_core(config::proxy_config& config) : proxy_config_(&config), task_runner_(config.get_tasks_config())
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
        cgroup::proxy_cgroup_manager cg_mngr_;
        tasks::task_manager task_runner_;

        void init_proxy_logger()
        {

        }

        void proxy_env_setup()
        {
            init_proxy_logger();
            mount_mngr_.mount_all();
            cg_mngr_.run();
            chroot_setup();
        }

        void generate_proxy_report(const config::task_report& task_report)
        {

        }
        
        void chroot_setup()
        {
            
        }
        
        void mount_setup()
        {
            mount_mngr_.mount_all();
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

        config::root_stats run_tasks_directly()
        {
            for(auto&& task_intfc : root_intfc_.tasks())
            {
                tasks::task_supervisor task_(*task_intfc);
                auto stats = task_.run_task();
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.cg_total_time_usec, stats.cg_total_mem_bytes);
            }
            return config::root_stats();
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
            env_setup_for_proxy(); 
        }
        
        void env_setup_for_proxy()
        {

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

                proxy_core proxy(proxy_conf);
                proxy.run();

                // We will never get here
                terminate("Something very weird happened");
            }
            return outside_pid;
        }
        
        void wait_for_proxy(pid_t proxy_pid)
        {
            int stat{};
            auto p = waitpid(proxy_pid, &stat, 0);

            if (p == proxy_pid)
            {
                logs::debug("Proxy exited. Signal: {}, RV : {}, Errno: {}", WTERMSIG(stat), p, errno);
            }
            else terminate("waitpid() for the proxy process failed. Stat: {}, Errno: {}", stat, errno);
        }
        
        void generate_results()
        {
            root_intfc_.generate_results();
        }
    };
    
 
    
}

#endif