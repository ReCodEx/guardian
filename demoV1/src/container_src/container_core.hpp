#ifndef CONTAINER_CORE
#define CONTAINER_CORE


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
        root_container_core(config::root_interface& conf) : root_intfc_(&conf)
        {
            logs::init_default_logger();
            logs::info("Hello world from container!");
        }

        config::root_stats execute_tasks_directly()
        {
            for(auto&& task_intfc : root_intfc_->tasks())
            {
                tasks::task_t task_(*task_intfc);
                auto stats = task_.run_task();
                logs::debug("Task finished with exit code: {}, in {} ms and {} bytes of used memory", stats.exit_code, stats.total_time_usec, stats.total_mem_bytes);
            }
            return config::root_stats();
        }

        int run_with_proxy_process()
        {
            return 0;
        }

    private:
        cgrp::root_cgroup_manager cgrp_mngr;
        config::root_interface* root_intfc_;
    };
}

#endif