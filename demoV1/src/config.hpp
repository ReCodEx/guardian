#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <vector>
#include <optional>
#include <memory>
#include <regex>
#include <tuple>

#include "cgrps.hpp"
#include "namespaces.hpp"
#include "utils.hpp"
#include "terminate.hpp"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "yaml-cpp/yaml.h"

#include <signal.h>

namespace config
{
    namespace cgrp = cgroup;
    namespace fs = std::filesystem;
    namespace options = boost::program_options;

    struct task_config;


    constexpr int DEFAULT_CLONE_FLAGS = CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWCGROUP | CLONE_NEWUTS;  //user namespaces might not always be supported

    namespace config_options
    {
        constexpr auto TASKS = "tasks";
        namespace task 
        {

            constexpr auto TASK_NAME = "task-id";
            constexpr auto EXEC_PATH = "path";
            constexpr auto EXEC_ARGS = "args";
            
            constexpr auto RLIMS = "rlims";

            constexpr auto AS_SIZE = "as-size";
            constexpr auto CPU_TIME = "cpu-time";
            constexpr auto WALL_TIME = "wall-time";
            constexpr auto MEMORY = "mem";
            constexpr auto PROCESSES = "processes";
            constexpr auto DISK_USAGE = "disk-usage";
        }

        constexpr auto ENVIRONMENT = "env";
        namespace env
        {
            constexpr auto DIRECTORY_RULES = "dir-rules";
            constexpr auto USE_DEFAULT_DIR_RULES = "use-defaults";
            constexpr auto BOX_ROOT = "box-root";
        }

        constexpr auto TASK_CG = "task-cg";
        constexpr auto STATS_YAML = "stats-yaml";
        constexpr auto CONFIG_YAML = "yaml";
    }

    namespace stats_names
    {
        constexpr auto STATUS = "status";
        constexpr auto OK = "ok";
        constexpr auto KILLED = "killed";
        constexpr auto NON_ZERO_EXIT_CODE = "non zero exit code";
        constexpr auto SIGNAL = "signal";
        constexpr auto EXIT_CODE = "exit-code";
        constexpr auto CG_TOTAL_TIME_USEC = "cg_total_time_usec";
        constexpr auto CG_TOTAL_MEM_BYTES = "cg_total_mem_bytes";
        constexpr auto RUSAGE_TOTAL_TIME_USEC = "rusage_total_time_usec";
        constexpr auto RUSAGE_TOTAL_MEM_BYTES = "rusage_total_mem_bytes";
    }

    namespace yaml_utils
    {
        template<typename T>
        inline std::vector<T> get_vector(const YAML::Node& seq)
        {
            std::vector<T> v;
            for(auto i = 0; i < seq.size(); i++)
            {
                v.emplace_back(seq[i].as<T>());
            }
            return std::move(v);
        }
    }

    class resource_limits
    {
    public:
        resource_limits() {}
        resource_limits(size_t cpu_time, size_t mem) : cpu_time_s_(cpu_time), memory_bytes_(mem) 
        {}

        resource_limits(const YAML::Node& limits_node)
        {
            if(limits_node[config_options::task::CPU_TIME]) cpu_time_s_ = limits_node[config_options::task::CPU_TIME].as<size_t>();
            if(limits_node[config_options::task::MEMORY]) memory_bytes_ = limits_node[config_options::task::MEMORY].as<size_t>();
            if(limits_node[config_options::task::WALL_TIME]) wall_time_s_ = limits_node[config_options::task::WALL_TIME].as<size_t>();
            if(limits_node[config_options::task::PROCESSES]) forked_processes_ = limits_node[config_options::task::PROCESSES].as<size_t>();
            if(limits_node[config_options::task::DISK_USAGE]) disk_usage_bytes_ = limits_node[config_options::task::DISK_USAGE].as<size_t>();
        }

        resource_limits(const options::variables_map& options_map)
        {
            if (options_map.contains(config_options::task::MEMORY)) 
            {
                memory_bytes_ = options_map[config_options::task::MEMORY].as<size_t>();
            }
            if (options_map.contains(config_options::task::CPU_TIME)) 
            {
                cpu_time_s_ = options_map[config_options::task::CPU_TIME].as<size_t>();
            }
            if (options_map.contains(config_options::task::WALL_TIME)) 
            {
                wall_time_s_ = options_map[config_options::task::WALL_TIME].as<size_t>();
            }
            if (options_map.contains(config_options::task::AS_SIZE)) 
            {
                as_size_bytes_ = options_map[config_options::task::AS_SIZE].as<size_t>();
            }
        }

        auto cpu_time() const { return cpu_time_s_; }
        auto memory() const { return memory_bytes_; }
        auto wall_time() const { return wall_time_s_; }
        auto as_size() const { return as_size_bytes_; }
        auto processes() const { return forked_processes_; }
        auto disk_usage() const { return disk_usage_bytes_; }

        void set_cpu_time(size_t s) { cpu_time_s_ = s; }
        void set_memory(size_t bytes) { memory_bytes_ = bytes; }
        void set_wall_time(size_t s) { wall_time_s_ = s; }
        void set_as_size(size_t s) { as_size_bytes_ = s; }
        void set_processes(size_t n) { forked_processes_ = n; }
    private:
        std::optional<size_t> cpu_time_s_; 
        std::optional<size_t> memory_bytes_;
        std::optional<size_t> as_size_bytes_;
        std::optional<size_t> forked_processes_;
        std::optional<size_t> disk_usage_bytes_;
        size_t wall_time_s_ = DEFAULT_WALL_TIME; 
        
        static constexpr size_t DEFAULT_WALL_TIME = 20;
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
            tasks_.emplace_back(task);
        }
    private:
        std::vector<task_stats> tasks_;
    };

    class task_config
    {
    public:
/*         task_config(const fs::path& exec, const std::vector<std::string>& args, const r_limits& rlims, const fs::path& cg_rel_path) : 
        exec_(exec), args_(args), rlimits_(rlims), cg_rel_path_(cg_rel_path) 
        {}

        task_config(fs::path&& exec, std::vector<std::string>&& args, r_limits&& rlims, fs::path&& cg_rel_path): 
        exec_(std::move(exec)), args_(std::move(args)), rlimits_(std::move(rlims)), cg_rel_path_(std::move(cg_rel_path))
        {} */

        task_config(const YAML::Node& task_node)
        {
            if(!task_node[config_options::task::EXEC_PATH])
                { terminate("Missing path to executable for task \"{}\"", name_); }
            
            exec_ = fs::path(task_node[config_options::task::EXEC_PATH].as<std::string>());

            if(task_node[config_options::task::TASK_NAME]) name_ = task_node[config_options::task::TASK_NAME].as<std::string>();
            
            if(task_node[config_options::task::EXEC_ARGS]) args_ = yaml_utils::get_vector<std::string>(task_node[config_options::task::EXEC_ARGS]);

            if(task_node[config_options::STATS_YAML]) stats_path_ = task_node[config_options::STATS_YAML].as<std::string>();
            
            if(task_node[config_options::task::RLIMS]) rlimits_ = resource_limits(task_node[config_options::task::RLIMS]);

            cg_rel_path_ = name_; 
        }

        task_config(const options::variables_map& options_map) : rlimits_(options_map)
        {
            if(!options_map.contains(config_options::task::EXEC_PATH))
                { terminate("No path to executable provided"); }
                
            exec_ = fs::path(options_map[config_options::task::EXEC_PATH].as<std::string>());

            if(options_map.contains(config_options::task::EXEC_ARGS))
            {
                args_ = options_map[config_options::task::EXEC_ARGS].as<std::vector<std::string>>();
            }

            if(options_map.contains(config_options::TASK_CG))
            {
                cg_rel_path_ = fs::path(options_map[config_options::TASK_CG].as<std::string>());
            }
        }
        
              auto& exec_args()             { return args_; }
        const auto& name()          const   { return name_; }
        const auto& exec_path()     const   { return exec_; }
        const auto& rlimits()       const   { return rlimits_; }
        const auto& cg_rel_path()   const   { return cg_rel_path_; }
        const auto& stats_path()    const   { return stats_path_; }

        void finalize_task(const task_stats& stats)
        {
            task_stats_ = stats;
            if(stats_path_.has_value())
            {
                generate_stats_yaml(stats_path_.value(), task_stats_.value());
            }
        }

    private:
        std::string name_;
        fs::path exec_;
        std::vector<std::string> args_;
        resource_limits rlimits_;
        fs::path cg_rel_path_;

        std::optional<fs::path>   stats_path_;
        std::optional<task_stats> task_stats_;

        static void generate_stats_yaml(const fs::path& path, const task_stats& stats)
        {
            YAML::Emitter yaml;
            yaml << YAML::BeginMap;
            yaml << YAML::Key << stats_names::STATUS; 
            if(stats.exited_normally && stats.exit_code == 0)
            {
                yaml << YAML::Value << stats_names::OK;
            }
            else if (stats.signalled)
            {
                yaml << YAML::Value << stats_names::KILLED;
            }
            else if (stats.exit_code)
            {
                yaml << YAML::Value << stats_names::NON_ZERO_EXIT_CODE;
            }

            yaml << YAML::Key << stats_names::EXIT_CODE << YAML::Value << stats.exit_code; 
            yaml << YAML::Key << stats_names::SIGNAL << YAML::Value << stats.signal; 
            yaml << YAML::Key << stats_names::CG_TOTAL_TIME_USEC << YAML::Value << stats.cg_total_time_usec; 
            yaml << YAML::Key << stats_names::CG_TOTAL_MEM_BYTES << YAML::Value << stats.cg_total_mem_bytes; 
            
            std::ofstream f(path);
            f << yaml.c_str(); 
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
                tasks_.emplace_back(std::make_unique<task_config>(tasks_node[i]));
            }
        }
        auto& get_tasks() const
        {
            return tasks_;
        }
    private:    
        std::vector<std::unique_ptr<task_config>> tasks_;    

        void parse_tasks(const YAML::Node& proxy_node)
        {
            auto tasks_node = proxy_node[config_options::TASKS];
            for(auto i = 0; i < tasks_node.size(); i++)
            {
                tasks_.emplace_back(std::make_unique<task_config>(tasks_node[i]));
            }
        }
    };

    class dir_rule
    {
    public:
        dir_rule(const std::string& rule, const fs::path& box_root) : rule_(rule)
        {
            construct_rule(rule, box_root);
        }
         
        const fs::path& in_dir() const                  { return inner_; }
        const fs::path& out_dir() const  { return outer_; }
        const std::string& string() const                 { return rule_; }
        bool rw() const             { return rw_; }
        bool dev() const            { return dev_; }
        bool noexec() const         { return noexec_; }
        bool maybe() const          { return maybe_; }
        bool fs() const             { return fs_; }
        bool tmp() const            { return tmp_; }
        bool norec() const          { return norec_; }
        bool allow_newdir() const   { return allow_newdir_; }
        
    private:
        static constexpr auto rule_regex_ = "([^=:]+)(=([^:]+))?(:(.+))?";
        std::string rule_;
        fs::path inner_;
        fs::path outer_;

        bool rw_            = false;
        bool dev_           = false;
        bool noexec_        = false;
        bool maybe_         = false;
        bool fs_            = false;
        bool tmp_           = false;
        bool norec_         = false;
        bool allow_newdir_  = false;
    
        void construct_rule(const std::string& rule, const fs::path& box_root)
        {
            std::regex rule_regex(rule_regex_);
            std::smatch m;
            if(!std::regex_match(rule, m, rule_regex))
                { terminate("Invalid fs-rule syntax: {}", rule); }

            auto& inner_token = m[1];
            auto& outer_token = m[3];
            auto& options_token = m[5];

            fs::path inner = fs::path(inner_token);

            std::optional<fs::path> outer;
            if ( outer_token != "" ) { outer = std::optional<fs::path>(outer_token); }

            std::vector<std::string> options = string_utils::split(options_token);
            
            if(!check_inner_dir(inner))
                { terminate("Invalid inner path in fs-rule: {}", inner_token.str()); }
            if(!check_outer_dir(outer)) 
                { terminate("Invalid outer path in fs-rule: {}", outer_token.str()); } 
            if(!check_options(options))
                { terminate("Invalid options in fs-rule: {}", options_token.str()); }

            parse_options(options);
            inner_ = box_root / inner;
            outer_ = fs::path("/") / (outer ? outer.value() : inner);
        }
        
        void parse_options(const std::vector<std::string>& options)
        {
            for(auto&& o : options)
            {
                if(o == "rw")       { rw_ = true; }
                if(o == "dev")      { dev_ = true; }
                if(o == "noexec")   { noexec_ = true; }
                if(o == "maybe")    { maybe_ = true; }
                if(o == "fs")       { fs_ = true; }
                if(o == "tmp")      { tmp_ = true; }
                if(o == "norec")    { norec_ = true; }
                if(o == "allow_newdir")    { allow_newdir_ = true; }
            }
        }
        
        static bool check_inner_dir(const fs::path& in)
        {
            return file_utils::is_valid_path(in) && file_utils::is_subdirectory(in);
        }

        static bool check_outer_dir(const std::optional<fs::path>& out)
        {
            return !out.has_value() || file_utils::is_valid_path(out.value());
        }
        
        static bool check_options(const std::vector<std::string>& options)
        {
            return true;
        }
    };

    class box_fs_config
    {
    public:
        box_fs_config() {}
        box_fs_config(const fs::path& box_root, const YAML::Node& env_node) : box_root_(box_root)
        {
            if(env_node[config_options::env::USE_DEFAULT_DIR_RULES]) use_defaults_ = env_node[config_options::env::USE_DEFAULT_DIR_RULES].as<bool>();
            add_default_rules();
            parse_rules(env_node[config_options::env::DIRECTORY_RULES]);
        }

        const auto& rules() const
        {
            return rules_;
        }

        const auto& box_root() const
        {
            return box_root_;
        }
        
        bool use_default_rules() const
        {
            return use_defaults_;
        }
        
        const auto& default_rules() const 
        {
            return default_rules_;
        }
              
    private:
        fs::path box_root_;
        std::vector<dir_rule>   rules_;
        

        bool use_defaults_ = true;
        std::vector<dir_rule>   default_rules_;

        void parse_rules(const YAML::Node& rules_list)
        {
            for(auto i = 0; i < rules_list.size(); i++)
            {
                rules_.emplace_back(dir_rule(rules_list[i].as<std::string>(), box_root_));
            }
        }
        
        void add_default_rules()
        {
            //default_rules_.emplace_back(dir_rule("box=./box:rw"));
            default_rules_.emplace_back(dir_rule("bin", box_root()));
            default_rules_.emplace_back(dir_rule("dev:dev", box_root()));
            default_rules_.emplace_back(dir_rule("lib", box_root()));
            default_rules_.emplace_back(dir_rule("lib64:maybe,rw", box_root()));
            default_rules_.emplace_back(dir_rule("proc=proc:fs", box_root()));
            //default_rules_.emplace_back(dir_rule("tmp:tmp"));
            default_rules_.emplace_back(dir_rule("usr", box_root())); 
        }
    };

    class credentials_config
    {
    public:
        credentials_config()
        {

        }



    private:
        int box_id;
        uid_t box_uid_range_start;
        gid_t box_gid_range_start;
    };
    
    class proxy_config
    {
    public:
        proxy_config() {}
        proxy_config(const YAML::Node& proxy_node) : tasks_(proxy_node[config_options::TASKS])
        {
            if(proxy_node[config_options::env::BOX_ROOT]) box_root_ = fs::path(proxy_node[config_options::env::BOX_ROOT].as<std::string>());
            box_fs_ = box_fs_config(box_root_, proxy_node[config_options::ENVIRONMENT]);
        }
        
        const auto& get_tasks_config() const
        {
            return tasks_;
        }
        
        const auto& fs_config() const
        {
            return box_fs_;
        }

        const auto& box_root() const
        {
            return box_root_;
        }
    private:
        fs::path box_root_;
        tasks_config tasks_;
        box_fs_config box_fs_;
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

        auto& tasks() const
        {
            return tasks_;
        }
        
        auto& get_proxy_config() const
        {
            return proxy_config_;
        }
        
        void generate_results()
        {

        }

    private:
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
                (config_options::CONFIG_YAML, options::value<std::string>(), "read the configuration from a yaml config file")
                ;

            options::options_description exec("Options to specify the executable and arguments");
            exec.add_options()
                (config_options::task::EXEC_PATH, options::value<std::string>(), "path to the program")
                (config_options::task::EXEC_ARGS, options::value<std::vector<std::string>>(), "list of arguments for the program")
                ;

            options::options_description rsrcs("Options for resource limitation");
            rsrcs.add_options()
                (config_options::task::MEMORY, options::value<size_t>(), "maximum amount of used virtual memory")
                (config_options::task::AS_SIZE, options::value<size_t>(), "address space size limit")
                (config_options::task::CPU_TIME, options::value<size_t>(), "cpu time limit")
                (config_options::task::WALL_TIME, options::value<size_t>(), "wall time limit")
                ;

            options::options_description results("Options for generating files with task results");
            results.add_options()
                (config_options::STATS_YAML, options::value<std::string>(), "path to yaml file with task results")
                ;

            options::options_description cgroups("Options for cgroup configuration");
            cgroups.add_options()
                (config_options::TASK_CG, options::value<std::string>(), "relative path to cgroup from the default that will be created for the task")
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
            
            if (options_map.contains(config_options::CONFIG_YAML))
            {
                configure_from_yaml(fs::path(options_map[config_options::CONFIG_YAML].as<std::string>()));
            }
            else
            {
                tasks_.emplace_back(std::make_unique<task_config>(options_map));
                return;
            }
        }

        void configure_from_yaml(const fs::path& f)
        {
            YAML::Node config = YAML::LoadFile(f);
            proxy_config_ = proxy_config(config);          
        }
        
    };
}


#endif