#ifndef PROCESS
#define PROCESS

#include "cgrps.hpp"
#include "config.hpp"
#include "filesystem"

namespace process
{
    namespace cgrp = cgrp_management;
    namespace fs = std::filesystem;

    class root_container_supervisor
    {
        cgrp::root_cgroup_manager cgrp_mngr;
    public:
        root_container_supervisor(const config::main_config& config_struct) : cgrp_mngr(config_struct._cgrp)
        {}

        int run_directly()
        {
            return 0;
        }

        int run_with_proxy_process()
        {
            return 0;
        }

    private:
    };

    class child_process_supervisor
    {

    public:
        int launch_task(){return 0;}
    };

    class task_launcher
    {
    public:
        int run_task(fs::path executable, const std::string& args ...)
        {
            return 0;
        }
    };
}

#endif