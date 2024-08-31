#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <vector>
#include <optional>

#include "cgrps.hpp"
#include "namespaces.hpp"
#include "utils.hpp"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

namespace config
{
    namespace cgrp = cgrp_management;
    namespace fs = std::filesystem;
    namespace options = boost::program_options;

    struct task_config;

    class root_config
    {
    public:
        bool ready_tasks()
        {
            return false;
        }

        void add_task(std::unique_ptr<task_config>&& task)
        {
            task_configs_.push_back(std::move(task));
        }
    private:
        std::vector<std::unique_ptr<task_config>> task_configs_;
    };

    class resource_limits
    {
    public:
        resource_limits() {}
        resource_limits(unsigned int cpu_time, unsigned int mem) : cpu_time_s_(cpu_time), memory_bytes_(mem) 
        {}

        auto cpu_time() { return cpu_time_s_; }
        auto memory() { return memory_bytes_; }

        void set_cpu_time(unsigned int s) { cpu_time_s_ = s; }
        void set_memory(unsigned int bytes) { memory_bytes_ = bytes; }
    private:
        std::optional<unsigned int> cpu_time_s_; 
        std::optional<unsigned int> memory_bytes_;
    };

    struct proxy_config
    {
       std::vector<std::unique_ptr<task_config>> tasks;
    };

    struct task_config
    {
        fs::path executable;
        std::vector<std::string> args;
        resource_limits rlims;
        fs::path cgrp_path;

        /*
        currently unused
        */
        unsigned int stack_size;
    };

    class configurator
    {
    public:
        bool ready_tasks()
        {
            return root_config_.ready_tasks();
        }

        const root_config& get_root_config(int argc, char** argv)
        {
            return root_config_;
        }

        int parse_options(int argc, char** argv)
        {
            options::options_description general("General options");
            general.add_options()
                ("help", "produce a help message")
                ("help-module", options::value<std::string>(),
                    "produce a help for a given module")
                ("version", "output the version number")
                ("f", options::value<std::string>(), "read the configuration from a config file")
                ;

            options::options_description exec("Options to specify the executable and arguments");
            exec.add_options()
                ("path", options::value<std::string>(), "path to the program")
                ("args", options::value<std::vector<std::string>>(), "list of arguments for the program")
                ;

            options::options_description rsrcs("Options for resource limitation");
            rsrcs.add_options()
                ("mem", options::value<int>(), "maximum amount of used virtual memory")
                ("mem-total", options::value<int>(), "address space size limit")
                ("time", options::value<int>(), "cpu time limit")
                ;

                
            // Declare an options description instance which will include
            // all the options
            options::options_description all("Allowed options");
            all.add(general).add(rsrcs);

            // Declare an options description instance which will be shown
            // to the user
            options::options_description visible("Allowed options");
            visible.add(general).add(rsrcs);
            

            options::variables_map vm;
            options::store(options::parse_command_line(argc, argv, all), vm);

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
            if (vm.count("f"))
            {
                return parse_config_file();
            }

            task_config task;


            if (vm.count("memory")) 
            {
                std::cout << "The 'memory' option was set to "
                    << vm["memory"].as<int>() << "\n";

            }
            if (vm.count("cpu")) {
                std::cout << "The 'cpu' option was set to "
                    << vm["cpu"].as<int>() << "\n";            
            }
            return 0;
        }
    private:
        root_config root_config_;

        int parse_config_file()
        {
            return 0;
        }
    };

}



#endif