#ifndef CONTAINER_CORE
#define CONTAINER_CORE

#include "config.hpp"
#include "process.hpp"
#include "tasks.hpp"

#include <filesystem>


namespace container_core
{
    using namespace tasks;
    namespace cgrp = cgroup;
    namespace fs = std::filesystem;

    class root_container_core
    {
    public:

        root_container_core(int argc, char** argv) : root_intfc_(argc, argv)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        config::root_stats run_tasks_directly()
        {
            for(auto&& task_intfc : root_intfc_.tasks())
            {
                tasks::task_t task_(*task_intfc);
                auto stats = task_.run_task();
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.cg_total_time_usec, stats.cg_total_mem_bytes);
            }
            return config::root_stats();
        }

        void run_tasks_with_proxy()
        {
            setup_for_proxy();
            run_proxy();
            generate_results();
        }

        void generate_results()
        {
            root_intfc_.generate_results();
        }

    private:
        
        config::root_interface root_intfc_;
        cgroup::cgroupv2_t root_cgrp_;
        
        void setup_for_proxy()
        {
            cg_setup_for_proxy();
            env_setup_for_proxy(); 
        }
        
        void cg_setup_for_proxy()
        {

        }

        void env_setup_for_proxy()
        {

        }

        void run_proxy()
        {

        } 
    };
    
    class proxy_container_core
    {
    public:

        proxy_container_core(config::root_interface& root_intfc) : root_intfc_(&root_intfc)
        {}

        void run_proxy()
        {
            logs::info("Hello world from the proxy!");
            proxy_setup();
            chroot();
            run_tasks();
            generate_results();
        }

    private:
        
        config::root_interface* root_intfc_;
        cgroup::cgroupv2_t root_cgrp_;
        
        void init_proxy_logger()
        {

        }

        void proxy_setup()
        {

            init_proxy_logger();
            namespace_setup();
            chroot_setup();
        }

        void chroot()
        {
        }
        
        config::root_stats run_tasks()
        {
            for(auto&& task_intfc : root_intfc_->tasks())
            {
                tasks::task_t task_(*task_intfc);
                auto stats = task_.run_task();
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.cg_total_time_usec, stats.cg_total_mem_bytes);
            }
            return config::root_stats();
        }

        void generate_results()
        {
            root_intfc_->generate_results();
        }
        
        void namespace_setup()
        {
            
        }
        
        void chroot_setup()
        {
            
        }
    };
}

#endif