#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "cgrps.hpp"
#include "utils.hpp"
#include "process.hpp"
#include "logs.hpp"
#include "container_core.hpp"



using namespace boost;
using namespace boost::program_options;

namespace fs = std::filesystem;

int main(int argc, char ** argv)
{
    spdlog::set_level(spdlog::level::debug);
    config::config_parser config_factory;
    auto root_config = config_factory.generate_root_config();

    container_core::root_container_core container(root_config);
    /*
    std::vector<std::string> args(argv + 1, argv + argc);

    options_description general("General options");
        general.add_options()
            ("help", "produce a help message")
            ("help-module", value<std::string>(),
                "produce a help for a given module")
            ("version", "output the version number")
            ;

        options_description rsrcs("Options for cgroup resource controllers");
        rsrcs.add_options()
            ("memory", value<int>(), "cgroup memory limit")
            ("cpu", value<int>(), "cgroup cpu shares")
            ;

            
        // Declare an options description instance which will include
        // all the options
        options_description all("Allowed options");
        all.add(general).add(rsrcs);

        // Declare an options description instance which will be shown
        // to the user
        options_description visible("Allowed options");
        visible.add(general).add(rsrcs);
           

        variables_map vm;
        store(parse_command_line(argc, argv, all), vm);

        if (vm.count("help")) 
        {
            std::cout << visible;
            return 0;
        }
        if (vm.count("help-module")) {
            const std::string& s = vm["help-module"].as<std::string>();
            if (s == "rsrcs") {
                std::cout << rsrcs;
            } else {
                std::cout << "Unknown module '" 
                     << s << "' in the --help-module option\n";
                return 1;
            }
            return 0;
        }
        if (vm.count("memory")) {
            std::cout << "The 'memory' option was set to "
                 << vm["memory"].as<int>() << "\n";            
        }
        if (vm.count("cpu")) {
            std::cout << "The 'cpu' option was set to "
                 << vm["cpu"].as<int>() << "\n";            
        }

    //cgrp_management::cgroupv2_t cgroup("/Experiment");
    /*
    std::string cgrp_path("/sys/fs/cgroup/Example1");
    cgrp_management::cgroupv2_t cgroup(cgrp_path);
    cgrp_management::cpu_cntrlr cpu(cgrp_path);
    cgrp_management::memory_cntrlr memory(cgrp_path);

     std::cout << "Enabling cpu controller..." << std::endl;
    cpu.enable_cntrlr_root();

    std::cout << "Setting cpu limit..." << std::endl;
    cpu.set_cpu_max(69);

    std::cout << "Setting memory limit..." << std::endl;
    memory.set_memory_max(1000000);

    std::cout << "adding myself to cgroup... "<< std::endl;
    cgroup.list_procs();
    cgroup.add_me();
    std::cout << "Processes in the cgroup:" << std::endl;
    cgroup.list_procs();
    std::cout << "finished!" << std::endl;
    */
    config::task_config helloworld{"/sys/fs/cgroup/Example1", 
                                   0,
                                   "/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/helloworld",
                                   {"./helloworld"}
                                    };
    config::task_config bsearch{"/sys/fs/cgroup/BsearchTest", 
                                   0,
                                   "/home/simonkurz/mff/rcdx_cntnr/demoV1/test_binaries/bsearch",
                                   {}
                                    };
    config::task_config just_return{"/sys/fs/cgroup/just_return2", 
                                   0,
                                   "/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/just_return",
                                   {}
                                    };
    config::task_config memory_test{"/sys/fs/cgroup/memory_test", 
                                   0,
                                   "/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/memory_test",
                                   {"junk", "1500000"}
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
    /*
    config::task_config helloworld{"/sys/fs/cgroup/Example1", 
                                   1024*1024,
                                   "/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/helloworld",
                                   {"1"}
                                    };
    auto cargs = clone_utils::convert_to_arg_array(helloworld.args);
    static char *environ[] = { NULL };
    int e = execve(helloworld.executable.c_str(), cargs.data(), environ);
    std::cout << "Execve failed. Errno: " << errno << "\n";
    std::cout << "finished!" << std::endl;*/
     
}