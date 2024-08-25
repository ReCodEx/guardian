#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <vector>

#include "cgrps.hpp"
#include "namespaces.hpp"
#include "utils.hpp"

namespace config
{
    namespace cgrp = cgrp_management;
    namespace fs = std::filesystem;

    struct main_config
    {
        cgrp::cgrp_config _cgrp;

    };

    struct task_config
    {
        fs::path cgrp_path;
        unsigned int stack_size;
        fs::path executable;
        std::vector<std::string> args;
    };

    struct proxy_config
    {
       std::vector<std::unique_ptr<task_config>> tasks;
    };

    class config_parser
    {
    public:
        
    };

}



#endif