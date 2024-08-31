#ifndef CONTAINER_CORE
#define CONTAINER_CORE


#include "process.hpp"
#include "tasks.hpp"

#include <filesystem>


namespace container_core
{
    using namespace tasks;
    namespace cgrp = cgrp_management;
    namespace fs = std::filesystem;

    class root_container_core
    {
    public:
        root_container_core(const config::root_config& config_struct)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        int run_directly()
        {
            return 0;
        }

        int run_with_proxy_process()
        {
            return 0;
        }

    private:
        cgrp::root_cgroup_manager cgrp_mngr;
    };

    class proxy_container_core
    {
        config::proxy_config* _conf;

    public:
        proxy_container_core(config::proxy_config& conf) {}

        int run_tasks()
        {
            for(auto&& task : _conf->tasks)
            {
                tasks::task_t task_(task.get());
                std::cout << task_.run_task() << '\n'; 
            }
            return 0;
        }

        void collect_results()
        {

        }

    private:
    };

    inline int proxy_process(void* proxy_config_ptr)
    {
        config::proxy_config* conf(static_cast<config::proxy_config*>(proxy_config_ptr));
        proxy_container_core supervisor(*conf);
        supervisor.run_tasks();
        supervisor.collect_results();

        return 0;
    }
}

#endif