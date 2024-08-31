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

    config::configurator configurator;
    configurator.parse_options(argc, argv);
    if(!configurator.ready_tasks())
    {
        exit(0);
    }
    auto& root_config = configurator.get_root_config(argc, argv);

    config::task_config helloworld{"/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/helloworld",
                                     {"./helloworld"},
                                     {60, 1000000},
                                    "/sys/fs/cgroup/Example1", 
                                    0
                                    };
    config::task_config bsearch {"/home/simonkurz/mff/rcdx_cntnr/demoV1/test_binaries/bsearch",
                                {},
                                {60, 1000000},
                                "/sys/fs/cgroup/BsearchTest", 
                                0,
                                };
    config::task_config just_return{"/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/just_return",
                                    {},
                                   {60, 1000000},
                                    "/sys/fs/cgroup/just_return2", 
                                   0
                                    };
    config::task_config memory_test{"/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/memory_test_fuzzy",
                                   {"junk", "2000000"},
                                   {60, 1000000},
                                   "/sys/fs/cgroup/memory_test", 
                                   0,
                                    };
                                    


    tasks::task_t hello(&helloworld);
    tasks::task_t bs(&bsearch);
    tasks::task_t jr(&just_return);
    tasks::task_t mt(&memory_test);
    //hello.run_task();
    //bs.run_task();
    //jr.run_task();
    mt.run_task();
    std::cout << "finished!" << std::endl;
}