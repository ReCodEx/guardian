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
    namespace cgrp = cgroup;
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
            tasks_.push_back(std::move(task));
        }

        auto& tasks()
        {
            return tasks_;
        }
    private:
        std::vector<std::unique_ptr<task_config>> tasks_;
    };

    class r_limits
    {
    public:
        r_limits() {}
        r_limits(unsigned int cpu_time, unsigned int mem) : cpu_time_s_(cpu_time), memory_bytes_(mem) 
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

    struct task_stats
    {
        const int exit_code;
        const int err_no;
        const int signal;

        const size_t total_mem_bytes;
        const size_t total_time_usec;
    };

    class task_config
    {
    public:
        task_config(const fs::path& exec, const std::vector<std::string>& args, const r_limits& rlims, const fs::path& cg_rel_path) : 
        exec_(exec), args_(args), rlims_(rlims), cg_rel_path_(cg_rel_path) {}

        task_config(fs::path&& exec, std::vector<std::string>&& args, r_limits&& rlims, fs::path&& cg_rel_path): 
        exec_(std::move(exec)), args_(std::move(args)), rlims_(std::move(rlims)), cg_rel_path_(std::move(cg_rel_path)){}

        const auto& exec_path() const   { return exec_; }
        auto& exec_args()               { return args_; }
        const auto& rlims() const       { return rlims_; }
        const auto& cg_rel_path() const { return cg_rel_path_; }

        void assign_stats(std::unique_ptr<task_stats>&& stats)
        {
            task_stats_ = std::move(stats);
        }

    private:
        fs::path exec_;
        std::vector<std::string> args_;
        r_limits rlims_;
        fs::path cg_rel_path_;
        size_t stack_size;//currently unused

        std::unique_ptr<task_stats> task_stats_;
    };

    class configurator
    {
    public:
        bool ready_tasks()
        {
            return root_config_.ready_tasks();
        }

        root_config& get_root_config(int argc, char** argv)
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
                ("cg", options::value<std::string>(), "relative cgroup path (from /sys/fs/cgroup) to run the task in")
                ;

            options::options_description rsrcs("Options for resource limitation");
            rsrcs.add_options()
                ("mem", options::value<unsigned int>(), "maximum amount of used virtual memory")
                ("as", options::value<unsigned int>(), "address space size limit")
                ("time", options::value<unsigned int>(), "cpu time limit")
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
            r_limits rlims;
            fs::path cg_rel_path{"rcdx"};

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

            return std::make_unique<task_config>(std::move(path), std::move(args), std::move(rlims), std::move(cg_rel_path));
        }

        r_limits rlims_from_options(const options::variables_map& vm)
        {
            r_limits rlims;

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