#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "cgrps.hpp"
#include "utils.hpp"

using namespace boost;
using namespace boost::program_options;

int main(int argc, char ** argv)
{
    std::cout << "Hello World from container!" << std::endl;
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
    std::string cgrp_path("/sys/fs/cgroup/Example");

    cgrp_management::cpu_cntrlr controller(cgrp_path);
    controller.enable_cntrlr_root();
    controller.set_cpu_max(1234);
}