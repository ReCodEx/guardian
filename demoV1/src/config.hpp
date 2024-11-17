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

#include "yaml-cpp/yaml.h"

#include <signal.h>

namespace config
{
    namespace cgrp = cgroup;
    namespace fs = std::filesystem;
    namespace options = boost::program_options;
    namespace pt = boost::property_tree;

    struct task_config;

    constexpr size_t DEFAULT_WALL_TIME = 20;

    constexpr int DEFAULT_CLONE_FLAGS = CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWPID; // | CLONE_NEWCGROUP | CLONE_NEWUTS;  //user namespaces might not always be supported

    namespace config_names
    {
        constexpr std::string RLIMS = "rlims";
        constexpr std::string TASKS = "tasks";

        constexpr std::string TASK_NAME = "task-id";
        constexpr std::string AS_SIZE = "as-size";
        constexpr std::string CPU_TIME = "cpu-time";
        constexpr std::string WALL_TIME = "wall-time";
        constexpr std::string MEMORY = "mem";

        constexpr std::string EXEC_PATH = "path";
        constexpr std::string EXEC_ARGS = "args";
        constexpr std::string TASK_CG = "task-cg";
        constexpr std::string STATS_XML = "stats-xml";
        constexpr std::string STATS_YAML = "stats-yaml";
        constexpr std::string CONFIG_XML = "xml";
        constexpr std::string CONFIG_YAML = "yaml";
    }

    namespace stats_names
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

    namespace yaml_utils
    {
        template<typename T>
        inline std::vector<T> get_vector(const YAML::Node& seq)
        {
            std::vector<T> v;
            for(auto i = 0; i < seq.size(); i++)
            {
                v.push_back(seq[i].as<T>());
            }
            return std::move(v);
        }
    }

    class r_limits
    {
    public:
        r_limits() {}
        r_limits(size_t cpu_time, size_t mem) : cpu_time_s_(cpu_time), memory_bytes_(mem) 
        {}

        r_limits(const pt::ptree& limits_tree) : 
                                        cpu_time_s_(type_utils::to_std_optional(limits_tree.get_optional<size_t>(config_names::CPU_TIME))),
                                        memory_bytes_(type_utils::to_std_optional(limits_tree.get_optional<size_t>(config_names::MEMORY))),
                                        wall_time_s_(limits_tree.get(config_names::WALL_TIME, DEFAULT_WALL_TIME))
        {}
        
        r_limits(const YAML::Node& limits_node)
        {
            if(limits_node[config_names::CPU_TIME]) cpu_time_s_ = limits_node[config_names::CPU_TIME].as<size_t>();
            if(limits_node[config_names::MEMORY]) memory_bytes_ = limits_node[config_names::MEMORY].as<size_t>();
            if(limits_node[config_names::WALL_TIME]) wall_time_s_ = limits_node[config_names::WALL_TIME].as<size_t>();
        }

        r_limits(const options::variables_map& options_map)
        {
            if (options_map.contains(config_names::MEMORY)) 
            {
                memory_bytes_ = options_map[config_names::MEMORY].as<size_t>();
            }
            if (options_map.contains(config_names::CPU_TIME)) 
            {
                cpu_time_s_ = options_map[config_names::CPU_TIME].as<size_t>();
            }
            if (options_map.contains(config_names::WALL_TIME)) 
            {
                wall_time_s_ = options_map[config_names::WALL_TIME].as<size_t>();
            }
            if (options_map.contains(config_names::AS_SIZE)) 
            {
                as_size_bytes_ = options_map[config_names::AS_SIZE].as<size_t>();
            }
        }

        auto cpu_time() const { return cpu_time_s_; }
        auto memory() const { return memory_bytes_; }
        auto wall_time() const { return wall_time_s_; }
        auto as_size() const { return as_size_bytes_; }

        void set_cpu_time(size_t s) { cpu_time_s_ = s; }
        void set_memory(size_t bytes) { memory_bytes_ = bytes; }
        void set_wall_time(size_t s) { wall_time_s_ = s; }
        void set_as_size(size_t s) { as_size_bytes_ = s; }
    private:
        std::optional<size_t> cpu_time_s_; 
        std::optional<size_t> memory_bytes_;
        std::optional<size_t> as_size_bytes_;
        
        size_t wall_time_s_ = DEFAULT_WALL_TIME; 
    };

    struct root_stats
    {
        
    };
    
    struct proxy_stats
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
    
    class task_report
    {
    public:
        void insert(task_stats task)
        {
            tasks_.push_back(task);
        }
    private:
        std::vector<task_stats> tasks_;
    };

    class task_config
    {
    public:
        task_config(const fs::path& exec, const std::vector<std::string>& args, const r_limits& rlims, const fs::path& cg_rel_path) : 
        exec_(exec), args_(args), rlimits_(rlims), cg_rel_path_(cg_rel_path) 
        {}

        task_config(fs::path&& exec, std::vector<std::string>&& args, r_limits&& rlims, fs::path&& cg_rel_path): 
        exec_(std::move(exec)), args_(std::move(args)), rlimits_(std::move(rlims)), cg_rel_path_(std::move(cg_rel_path))
        {}

        task_config(const pt::ptree& task_tree) :   exec_(fs::path(task_tree.get<std::string>(config_names::EXEC_PATH))), 
                                            args_(std::move(string_utils::split(task_tree.get(config_names::EXEC_ARGS, "")))),
                                            rlimits_(task_tree.get_child(config_names::RLIMS)),
                                            cg_rel_path_(fs::path(task_tree.get<std::string>(config_names::TASK_CG))),
                                            stats_path_(type_utils::to_std_optional(task_tree.get_optional<std::string>(config_names::STATS_XML)))
        {}

        task_config(const YAML::Node& task_node)
        {
            if(task_node[config_names::EXEC_PATH]) exec_ = fs::path(task_node[config_names::EXEC_PATH].as<std::string>());
            else terminate("Missing path to executable for task \"{}\"", name_);
            
            if(task_node[config_names::TASK_NAME]) name_ = task_node[config_names::TASK_NAME].as<std::string>();
            
            if(task_node[config_names::EXEC_ARGS]) args_ = yaml_utils::get_vector<std::string>(task_node[config_names::EXEC_ARGS]);

            if(task_node[config_names::STATS_YAML]) stats_path_ = task_node[config_names::STATS_YAML].as<std::string>();
            
            if(task_node[config_names::RLIMS]) rlimits_ = r_limits(task_node[config_names::RLIMS]);

            cg_rel_path_ = name_; 
        }

        task_config(const options::variables_map& options_map) : rlimits_(options_map)
        {
            if(options_map.contains(config_names::EXEC_PATH))
            {
                exec_ = fs::path(options_map[config_names::EXEC_PATH].as<std::string>());
            }
            else
            {
                terminate("No path to executable provided");
            }

            if(options_map.contains(config_names::EXEC_ARGS))
            {
                args_ = options_map[config_names::EXEC_ARGS].as<std::vector<std::string>>();
            }

            if(options_map.contains(config_names::STATS_XML))
            {
                stats_path_ = fs::path(options_map[config_names::STATS_XML].as<std::string>());
            }

            if(options_map.contains(config_names::TASK_CG))
            {
                cg_rel_path_ = fs::path(options_map[config_names::TASK_CG].as<std::string>());
            }
        }
        
        const auto& name() const        { return name_; }
        const auto& exec_path() const   { return exec_; }
        auto& exec_args()               { return args_; }
        const auto& rlimits() const       { return rlimits_; }
        const auto& cg_rel_path() const { return cg_rel_path_; }
        const auto& stats_path() const  { return stats_path_; }

        void finalize_task(const task_stats& stats)
        {
            task_stats_ = stats;
            if(stats_path_.has_value())
            {
                create_stats_file(stats_path_.value(), task_stats_.value());
            }
        }

        void generate_stats_file(const fs::path& path)
        {
            if(!task_stats_)
            {
                terminate("Task hasn't been finalized!");
            }
            create_stats_file(path, task_stats_.value());
        }

    private:
        std::string name_;
        fs::path exec_;
        std::vector<std::string> args_;
        r_limits rlimits_;
        fs::path cg_rel_path_;

        std::optional<fs::path>   stats_path_;
        std::optional<task_stats> task_stats_;

        static void create_stats_file(const fs::path& path, const task_stats& stats)
        {
            pt::ptree stat_tree;
            std::ofstream f(path);
            if(stats.exited_normally && stats.exit_code == 0)
            {
                stat_tree.put(stats_names::STATUS, stats_names::OK);
            }
            else if (stats.signalled)
            {
                stat_tree.put(stats_names::STATUS, stats_names::KILLED);
            }
            else if (stats.exit_code)
            {
                stat_tree.put(stats_names::STATUS, stats_names::NON_ZERO_EXIT_CODE);
            }

            stat_tree.put(stats_names::CG_TOTAL_TIME_USEC, stats.cg_total_time_usec);
            stat_tree.put(stats_names::CG_TOTAL_MEM_BYTES, stats.cg_total_mem_bytes);
            stat_tree.put(stats_names::RUSAGE_TOTAL_TIME_USEC, stats.rusage_total_time_usec);
            stat_tree.put(stats_names::RUSAGE_TOTAL_MEM_BYTES, stats.rusage_total_mem_bytes);

            pt::write_xml(f, stat_tree);
        }
    };

    class root_config
    {
    public:
        root_config() {}
        root_config(YAML::Node config)
        {
            
        }
    };
    
    class tasks_config
    {
    public:
        tasks_config() {}
        tasks_config(const YAML::Node& tasks_node)
        {
            for(auto i = 0; i < tasks_node.size(); i++)
            {
                tasks_.push_back(std::make_unique<task_config>(tasks_node[i]));
            }
        }
        auto& get_tasks()
        {
            return tasks_;
        }
    private:    
        std::vector<std::unique_ptr<task_config>> tasks_;    

        void parse_tasks(const YAML::Node& proxy_node)
        {
            auto tasks_node = proxy_node[config_names::TASKS];
            for(auto i = 0; i < tasks_node.size(); i++)
            {
                tasks_.push_back(std::make_unique<task_config>(tasks_node[i]));
            }
        }
    };

    class proxy_config
    {
    public:
        proxy_config() {}
        proxy_config(const YAML::Node& proxy_node) : tasks_(proxy_node[config_names::TASKS])
        {
        }
        
        auto& get_tasks_config()
        {
            return tasks_;
        }

    private:
        tasks_config tasks_;
    };

    class root_interface
    {
    public:
        root_interface(int argc, char** argv) 
        {
            parse_options(argc, argv);
        }

        bool ready_tasks() const
        {
            return tasks_.size();
        }

        auto& tasks()
        {
            return tasks_;
        }
        
        auto& get_proxy_config()
        {
            return proxy_config_;
        }
        
        void generate_results()
        {

        }
    private:
        pt::ptree config_tree_;
        root_config root_config_;
        proxy_config proxy_config_;
        std::vector<std::unique_ptr<task_config>> tasks_;
        
        void parse_options(int argc, char** argv)
        {
            options::options_description general("General options");
            general.add_options()
                ("help", "produce a help message")
                ("help-module", options::value<std::string>(),
                    "produce a help for a given module")
                ("version", "output the version number")
                (config_names::CONFIG_XML.c_str(), options::value<std::string>(), "read the configuration from a config file")
                (config_names::CONFIG_YAML.c_str(), options::value<std::string>(), "read the configuration from a yaml config file")
                ;

            options::options_description exec("Options to specify the executable and arguments");
            exec.add_options()
                (config_names::EXEC_PATH.c_str(), options::value<std::string>(), "path to the program")
                (config_names::EXEC_ARGS.c_str(), options::value<std::vector<std::string>>(), "list of arguments for the program")
                ;

            options::options_description rsrcs("Options for resource limitation");
            rsrcs.add_options()
                (config_names::MEMORY.c_str(), options::value<size_t>(), "maximum amount of used virtual memory")
                (config_names::AS_SIZE.c_str(), options::value<size_t>(), "address space size limit")
                (config_names::CPU_TIME.c_str(), options::value<size_t>(), "cpu time limit")
                (config_names::WALL_TIME.c_str(), options::value<size_t>(), "wall time limit")
                ;

            options::options_description results("Options for generating files with task results");
            results.add_options()
                (config_names::STATS_XML.c_str(), options::value<std::string>(), "path to an xml file with task results")
                ;

            options::options_description cgroups("Options for cgroup configuration");
            cgroups.add_options()
                (config_names::TASK_CG.c_str(), options::value<std::string>(), "relative path to cgroup from the default that will be created for the task")
                ;
                
            // Declare an options description instance which will include
            // all the options
            options::options_description all("Allowed options");
            all.add(general).add(rsrcs).add(exec).add(results).add(cgroups);

            options::variables_map options_map;
            options::store(options::parse_command_line(argc, argv, all), options_map);

            if (options_map.contains("help")) 
            {
                std::cout << all;
                return;
            }
            if (options_map.contains("help-module")) {
                const std::string& s = options_map["help-module"].as<std::string>();
                if (s == "rsrcs") {
                    std::cout << rsrcs;
                } else {
                    std::cout << "Unknown module '" 
                        << s << "' in the --help-module option\n";
                    return;
                }
                return;
            }
            
            if (options_map.contains(config_names::CONFIG_XML))
            {
                configure_from_xml(fs::path(options_map[config_names::CONFIG_XML].as<std::string>()));
                return;
            }
            else if (options_map.contains(config_names::CONFIG_YAML))
            {
                configure_from_yaml(fs::path(options_map[config_names::CONFIG_YAML].as<std::string>()));
            }
            else
            {
                tasks_.push_back(std::make_unique<task_config>(options_map));
                return;
            }
        }

        void configure_from_xml(const fs::path& f)
        {
            pt::read_xml(f.string(), config_tree_);
            BOOST_FOREACH(pt::ptree::value_type &task_conf, config_tree_.get_child("tasks")) 
            {
                tasks_.push_back(std::make_unique<task_config>(task_conf.second));
            }
        }
        
        void configure_from_yaml(const fs::path& f)
        {
            YAML::Node config = YAML::LoadFile(f);
            root_config_ = root_config(config);
            proxy_config_ = proxy_config(config);          
        }
    };
}


#endif