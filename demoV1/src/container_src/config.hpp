#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <vector>
#include <optional>

#include "cgrps.hpp"
#include "namespaces.hpp"
#include "utils.hpp"
#include "terminate.hpp"

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
        bool ready_tasks() const
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

        auto cpu_time() const { return cpu_time_s_; }
        auto memory() const { return memory_bytes_; }

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

    class task_config
    {
    public:
        task_config(const fs::path& exec, const std::vector<std::string>& args, const resource_limits& rlims, const fs::path& cgrp_path_) : 
        exec_(exec), args_(args), rlims_(rlims), cgrp_path_(cgrp_path_) {}

        task_config(fs::path&& exec, std::vector<std::string>&& args, resource_limits&& rlims, fs::path&& cgrp_path_): 
        exec_(std::move(exec)), args_(std::move(args)), rlims_(std::move(rlims)), cgrp_path_(std::move(cgrp_path_)){}

        const auto& exec_path() { return exec_; }
        auto& exec_args() { return args_; }
        const auto& rlims()     { return rlims_; }
        const auto& cg_path()   { return cgrp_path_; }

    private:
        fs::path exec_;
        std::vector<std::string> args_;
        resource_limits rlims_;
        fs::path cgrp_path_;

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
            all.add(general).add(rsrcs).add(exec);

            // Declare an options description instance which will be shown
            // to the user
            options::options_description visible("Allowed options");
            visible.add(general).add(rsrcs).add(exec);
            

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
                return configure_from_file();
            }
            else
            {
                auto task = configure_task_from_options(vm);
                root_config_.add_task(std::move(task));
                return 0;
            }
        }
    private:
        root_config root_config_;

        int configure_from_file()
        {
            return 0;
        }

        std::unique_ptr<task_config> configure_task_from_options(const options::variables_map& vm)
        {
            fs::path path;
            std::vector<std::string> args;
            resource_limits rlims;
            fs::path cg_path;

            if(vm.contains("path"))
            {
                path = fs::path(vm["path"].as<std::string>());
            }
            else
            {
                terminate("No path to executable provided");
            }

            if(vm.contains("args"))
            {
                args = std::vector<std::string>(vm["args"].as<std::vector<std::string>>());
            }

            rlims = rlims_from_options(vm);

            return std::make_unique<task_config>(std::move(path), std::move(args), std::move(rlims), std::move(cg_path));
        }

        resource_limits rlims_from_options(const options::variables_map& vm)
        {
            resource_limits rlims;

            if (vm.contains("mem")) 
            {
                rlims.set_memory(vm["mem"].as<unsigned int>());
            }
            if (vm.contains("time")) 
            {
                rlims.set_cpu_time(vm["time"].as<unsigned int>());
            }

            return std::move(rlims);
        }
    };

}



#endif