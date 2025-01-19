#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "cgrps.hpp"
#include "utils.hpp"
#include "process.hpp"
#include "logs.hpp"
#include "container_core.hpp"


namespace fs = std::filesystem;

int main(int argc, char ** argv)
{
    spdlog::set_level(spdlog::level::debug);
    //container_core::root_core core(argc, argv);
    config::dir_rule rule("temp:fs");
    //core.run();
    
    std::cout << "finished!" << std::endl;
}