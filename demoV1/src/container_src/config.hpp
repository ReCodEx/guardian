#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <vector>
#include <optional>
#include <memory>

#include "cgrps.hpp"
#include "namespaces.hpp"
#include "utils.hpp"
#include "terminate.hpp"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>

namespace config
{
    namespace cgrp = cgroup;
    namespace fs = std::filesystem;
    namespace options = boost::program_options;
    namespace pt = boost::property_tree;

    struct task_intfc;

    constexpr size_t DEFAULT_WALL_TIME = 60;

    namespace option_names
    {
        constexpr std::string RLIMS = "rlims";
        constexpr std::string CPU_TIME = "cpu-time";
        constexpr std::string WALL_TIME = "wall-time";
        constexpr std::string MEMORY = "mem";

        constexpr std::string EXEC_PATH = "path";
        constexpr std::string TASK_CG = "task-cg";
        constexpr std::string STATS_PATH = "stats";
        constexpr std::string EXEC_ARGS = "args";
        constexpr std::string CONFIG_F = "f";
    }

    namespace results
    {
        constexpr std::string STATUS = "status";
        constexpr std::string OK = "ok";
        constexpr std::string KILLED = "killed";
        static std::string NON_ZERO_EXIT_CODE = "non zero exit code";
        static std::string CG_TOTAL_TIME_USEC = "cg_total_time_usec";
        static std::string CG_TOTAL_MEM_BYTES = "cg_total_mem_bytes";
        static std::string RUSAGE_TOTAL_TIME_USEC = "rusage_total_time_usec";
        static std::string RUSAGE_TOTAL_MEM_BYTES = "rusage_total_mem_bytes";
    }

    class r_limits
    {
    public:
        r_limits() {}
        r_limits(size_t cpu_time, size_t mem) : cpu_time_s_(cpu_time), memory_bytes_(mem) 
        {}

        r_limits(const pt::ptree& limits_tree) : 
                                        cpu_time_s_(type_utils::to_std_optional(limits_tree.get_optional<size_t>(option_names::CPU_TIME))),
                                        memory_bytes_(type_utils::to_std_optional(limits_tree.get_optional<size_t>(option_names::MEMORY))),
                                        wall_time_s_(limits_tree.get(option_names::WALL_TIME, DEFAULT_WALL_TIME))
        {}

        auto cpu_time() const { return cpu_time_s_; }
        auto memory() const { return memory_bytes_; }
        auto wall_time() const { return wall_time_s_; }

        void set_cpu_time(size_t s) { cpu_time_s_ = s; }
        void set_memory(size_t bytes) { memory_bytes_ = bytes; }
        void set_wall_time(size_t s) { wall_time_s_ = s; }
    private:
        std::optional<size_t> cpu_time_s_; 
        std::optional<size_t> memory_bytes_;
        
        size_t wall_time_s_ = DEFAULT_WALL_TIME; 
    };

    struct root_stats
    {
        
    };

    struct task_stats
    {
        bool exited_normally;
        bool signalled;
        int exit_code;
        int err_no;
        int signal;

        size_t cg_total_mem_bytes;
        size_t cg_total_time_usec;

        long rusage_total_mem_bytes;
        long rusage_total_time_usec;

    };

    class task_intfc
    {
    public:
        task_intfc(const fs::path& exec, const std::vector<std::string>& args, const r_limits& rlims, const fs::path& cg_rel_path) : 
        exec_(exec), args_(args), rlims_(rlims), cg_rel_path_(cg_rel_path) {}

        task_intfc(fs::path&& exec, std::vector<std::string>&& args, r_limits&& rlims, fs::path&& cg_rel_path): 
        exec_(std::move(exec)), args_(std::move(args)), rlims_(std::move(rlims)), cg_rel_path_(std::move(cg_rel_path)){}

        task_intfc(pt::ptree task_tree) :   exec_(fs::path(task_tree.get<std::string>(option_names::EXEC_PATH))), 
                                            args_(std::move(string_utils::split(task_tree.get<std::string>(option_names::EXEC_ARGS)))),
                                            rlims_(task_tree.get_child(option_names::RLIMS)),
                                            cg_rel_path_(fs::path(task_tree.get<std::string>(option_names::TASK_CG)))
        {}

        const auto& exec_path() const   { return exec_; }
        auto& exec_args()               { return args_; }
        const auto& rlims() const       { return rlims_; }
        const auto& cg_rel_path() const { return cg_rel_path_; }
        const auto& stats_path() const  { return stats_path_; }

        void assign_stats(const task_stats& stats)
        {
            task_stats_ = stats;
        }

        void generate_stats_file()
        {
            if(!task_stats_ || !stats_path_)
            {
                terminate("Missing task statistics or a path to write them to!");
            }
            create_stats_file(*stats_path_, *task_stats_);
        }

        void generate_stats_file(const fs::path& path)
        {
            if(!task_stats_)
            {
                terminate("Missing task statistics!");
            }
            create_stats_file(path, *task_stats_);
        }

    private:
        fs::path exec_;
        std::vector<std::string> args_;
        r_limits rlims_;
        fs::path cg_rel_path_;

        std::optional<fs::path>   stats_path_;
        std::optional<task_stats> task_stats_;

        static void create_stats_file(const fs::path& path, const task_stats& stats)
        {
            pt::ptree stat_tree;
            std::ofstream f(path);
            if(stats.exited_normally && stats.exit_code == 0)
            {
                stat_tree.put(results::STATUS, results::OK);
            }
            else if (stats.signalled)
            {
                stat_tree.put(results::STATUS, results::KILLED);
            }
            else if (stats.exit_code)
            {
                stat_tree.put(results::STATUS, results::NON_ZERO_EXIT_CODE);
            }

            stat_tree.put(results::CG_TOTAL_TIME_USEC, stats.cg_total_time_usec);
            stat_tree.put(results::CG_TOTAL_MEM_BYTES, stats.cg_total_mem_bytes);
            stat_tree.put(results::RUSAGE_TOTAL_TIME_USEC, stats.rusage_total_time_usec);
            stat_tree.put(results::RUSAGE_TOTAL_MEM_BYTES, stats.rusage_total_mem_bytes);

            pt::write_xml(f, stat_tree);
        }
    };

    class root_interface
    {
    public:
        root_interface(const fs::path& config_xml) {}
        root_interface(int argc, char** argv) 
        {
            parse_options(argc, argv);
        }

        bool ready_tasks() const
        {
            return tasks_.size();
        }

        void add_task(std::unique_ptr<task_intfc>&& task)
        {
            tasks_.push_back(std::move(task));
        }

        auto& tasks()
        {
            return tasks_;
        }

        //TODO: add support for stats option
        void generate_results()
        {
            tasks_[0]->generate_stats_file(fs::path("/home/simonkurz/mff/rcdx_cntnr/demoV1/src/build/TASK_RESULTS.txt"));
        }
    private:
        pt::ptree config_tree_;
        std::vector<std::unique_ptr<task_intfc>> tasks_;
        
        int parse_options(int argc, char** argv)
        {
            options::options_description general("General options");
            general.add_options()
                ("help", "produce a help message")
                ("help-module", options::value<std::string>(),
                    "produce a help for a given module")
                ("version", "output the version number")
                (option_names::CONFIG_F.c_str(), options::value<std::string>(), "read the configuration from a config file")
                ;

            options::options_description exec("Options to specify the executable and arguments");
            exec.add_options()
                (option_names::EXEC_PATH.c_str(), options::value<std::string>(), "path to the program")
                (option_names::EXEC_ARGS.c_str(), options::value<std::vector<std::string>>(), "list of arguments for the program")
                (option_names::TASK_CG.c_str(), options::value<std::string>(), "relative cgroup path (from /sys/fs/cgroup) to run the task in")
                ;

            options::options_description rsrcs("Options for resource limitation");
            rsrcs.add_options()
                (option_names::MEMORY.c_str(), options::value<size_t>(), "maximum amount of used virtual memory")
                ("as", options::value<size_t>(), "address space size limit")
                (option_names::CPU_TIME.c_str(), options::value<size_t>(), "cpu time limit")
                (option_names::WALL_TIME.c_str(), options::value<size_t>(), "wall time limit")
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
            
            if (vm.count(option_names::CONFIG_F))
            {
                configure_from_xml(fs::path(vm[option_names::CONFIG_F].as<std::string>()));
                return 0;
            }
            else
            {
                auto task = configure_task_from_options(vm);
                add_task(std::move(task));
                return 0;
            }
        }

        void configure_from_xml(const fs::path& f)
        {
            pt::read_xml(f.string(), config_tree_);
        }

        auto generate_tasks(pt::ptree config_tree)
        {
            std::vector<std::unique_ptr<task_intfc>> tasks;
        }

        static std::unique_ptr<task_intfc> configure_task_from_options(const options::variables_map& vm)
        {
            fs::path path;
            std::vector<std::string> args;
            r_limits rlims;
            fs::path cg_rel_path{"rcdx"};

            if(vm.contains(option_names::EXEC_PATH))
            {
                path = fs::path(vm[option_names::EXEC_PATH].as<std::string>());
            }
            else
            {
                terminate("No path to executable provided");
            }

            if(vm.contains(option_names::EXEC_ARGS))
            {
                args = std::vector<std::string>(vm[option_names::EXEC_ARGS].as<std::vector<std::string>>());
            }

            rlims = rlims_from_options(vm);

            return std::make_unique<task_intfc>(std::move(path), std::move(args), std::move(rlims), std::move(cg_rel_path));
        }

        static r_limits rlims_from_options(const options::variables_map& vm)
        {
            r_limits rlims;

            if (vm.contains(option_names::MEMORY)) 
            {
                rlims.set_memory(vm[option_names::MEMORY].as<size_t>());
            }
            if (vm.contains(option_names::CPU_TIME)) 
            {
                rlims.set_cpu_time(vm[option_names::CPU_TIME].as<size_t>());
            }
            if (vm.contains(option_names::WALL_TIME)) 
            {
                rlims.set_wall_time(vm[option_names::WALL_TIME].as<size_t>());
            }

            return std::move(rlims);
        }

    };
}


#endif