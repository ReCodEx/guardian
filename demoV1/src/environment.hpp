#ifndef CONTAINER_ENV
#define CONTAINER_ENV

#include <filesystem>

#include <sys/mount.h>
#include <errno.h>

#include "config.hpp"
#include "terminate.hpp"
#include "cgrps.hpp"
#include "credentials.hpp"

namespace env
{
    namespace fs = std::filesystem;

    /// @brief Manager class responsible for setting up the mount namespace of the proxy and tasks.
    class proxy_mount_manager
    {
    public:
        /// @brief 
        /// @param proxy_config Proxy node of the configuration.
        /// @param credentials Credentials manager stores the box root path. 
        proxy_mount_manager(const config::proxy_config& proxy_config, const credentials::proxy_credentials_manager& credentials) :  proxy_config_(&proxy_config),
                                                                                                                                    credentials_(&credentials)
        {}

        void run()
        {
            /// First rslave the box root so that mounts don't propagate.
            make_root_rslave();
            mount_pivot_dir();
            mount_cgroup();
        }
    private:
        /// @brief Proxy configuration node.
        const config::proxy_config* proxy_config_;
        
        /// @brief Stores the box root path.
        const credentials::proxy_credentials_manager* credentials_;

        /// @brief Create a mount point at the box root directory.
        void mount_pivot_dir()
        {
            //the directory that we pivot_root to has to be a mount point
            auto& box_root = credentials_->box_root();
            if(mount(box_root.c_str(), box_root.c_str(), nullptr, MS_REC | MS_BIND, nullptr))
                { terminate("failed to bind mount the pivot directory, errno: {}", errno); }
        }
        
        /// @brief Don't propagate mount events under box root to other namespaces.
        void make_root_rslave()
        {
            //don't propagate mount events to other namespaces
            
            if(mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr))
                { terminate("failed to change propagation type of root mount, errno: {}", errno); }
        }

        /// @brief Mount cgroup filesystem into the box.
        void mount_cgroup()
        {
            //remount cgroup filesystem into the box
            auto box_cg_root = credentials_->box_root() / fs::path("sys/fs/cgroup");

            logs::debug("Creating directory: {}",box_cg_root.string());

            if(!fs::create_directories(box_cg_root))
                { terminate("Failed to create directory for the cgroup fs ({})", box_cg_root.string()); } 

            if(umount(cgroup::CGROUP_FS_PATH().c_str()))
                { terminate("failed to unmount cgroup filesystem, errno: {}", errno); }
            
            if(mount("none", (box_cg_root).c_str(), "cgroup2", 0, nullptr))
                { terminate("failed to remount cgroup2 filesystem, errno: {}", errno); }
        }
    };

    /// @brief Supervisor class implementing a single directory rule (TODO: link to documentation)
    class dir_rule_supervisor
    {
    public:
        /// @brief 
        /// @param rule Reference to the rule configuration.
        /// @param creds Reference to credentials manager (to chown() box directories to box UID/GID).
        dir_rule_supervisor(const config::dir_rule& rule, const credentials::proxy_credentials_manager& creds) : rule_(&rule), credentials_(&creds)
        {}

        /// @brief Apply the rule - mount an outside/temporary directory or filesystem into the box.
        void apply()
        {
            /// Skip the rule if it has the maybe() flag and the outside directory
            /// doesn't exist.
            if(rule_->maybe() && !fs::is_directory(rule_->out_dir()))
            { 
                logs::debug("Skipping the mount of maybe() rule: \"{}\".", rule_->string()); 
                return;
            } 

            auto in = credentials_->box_root() / rule_->in_dir();
            auto& out = rule_->out_dir();
            auto flags = default_flags();

            if(rule_->fs())
            {
                if(mount("none", in.c_str(), out.c_str() + 1, flags, "") < 0)
                    { terminate("Mount failed for directory rule: {}, errno: {}", rule_->string(), errno); }
                
                // If we are mounting procfs, add hidepid=2, so that only the processes
	            // of the same user are visible. This has to be done as a remount.
                if(in.string() == "proc")
                {
                    if (mount("none", in.c_str(), out.c_str() + 1, MS_REMOUNT | flags , "hidepid=2") < 0)
		                { terminate("Cannot re-mount proc with hidepid option."); }
                }
            }
            else
            {
                flags |= MS_BIND | MS_NOSUID;
                if(!rule_->norec()) { flags |= MS_REC; }
                logs::debug("Mounting {} to {}", out.string(), in.string());
                if( mount(out.c_str(), in.c_str(), "none", flags, "") < 0 ||
                    mount(out.c_str(), in.c_str(), "none", MS_REMOUNT | flags, "") < 0)
                    { terminate("Mount failed for directory rule: {}, errno: {}", rule_->string(), errno); }
            }
        }
        
        /// @brief Create necessary directories for the rule.
        void create_directories()
        {
            /// Skip the rule if it has the maybe() flag and the outside directory
            /// doesn't exist.
            if(rule_->maybe() && !fs::is_directory(rule_->out_dir())) 
            {
                logs::debug("Skipping creation of directories for maybe() rule: \"{}\".", rule_->string());
                return; 
            } 

            create_inner_dir();
            if(rule_->tmp()) { create_outer_temp_dir(); }
            if(dummy_dir_) { create_dummy_dir(); }
        }
        
        /// @brief Remember a dummy directory created for this rule to clean it up later.
        /// @param dir Path of the directory.
        void add_dummy_dir(const fs::path& dir)
        {
            dummy_dir_ = dir;
        }
    private:
        /// @brief The rule configuration.
        const config::dir_rule* rule_;
        
        /// @brief Credentials manager to get box UID/GID.
        const credentials::proxy_credentials_manager* credentials_;
        
        /// @brief Remember a dummy directory if it was created, to clean it up later.
        std::optional<fs::path> dummy_dir_;
        
        /// @brief Create the inner directory of the rule.
        void create_inner_dir()
        {
            logs::debug("Checking for inner directory: {}", rule_->in_dir().string());
            create_dir(credentials_->box_root() / rule_->in_dir());
        }
    
        /// @brief Create an outer directory for a rule with the temp() flag.
        void create_outer_temp_dir()
        {
            logs::debug("Creating outer temporary directory: {}", rule_->out_dir().string());
            create_dir(rule_->out_dir());
        }

        /// @brief Create a dummy directory possibly required for a nested mount.(TODO: link to documentation)
        void create_dummy_dir()
        {
            logs::debug("Checking for dummy dir: {}", dummy_dir_.value().string());
            create_dir(dummy_dir_.value());
        }
        
        /// @brief Create a directory and chmod + chown it to the box credentials.
        /// @param dir Path of the directory.
        void create_dir(const fs::path& dir)
        {
            if(fs::is_directory(dir))
                { terminate("Directory ({}) to be created already exists!", dir.string()); } 

            logs::debug("Creating directory: {}",dir.string());

            if(!fs::create_directory(dir))
                { terminate("Failed to create outside directory ({})", dir.string()); } 
            
            if(chown(dir.c_str(), credentials_->box_uid(), credentials_->box_gid()) < 0)
                { terminate("chown() on outside temp directory ({}) failed, errno: {}", dir.string(), errno); }
            
            /// TODO: set proper permissions.
            if(chmod(dir.c_str(), 0777) < 0)
                { terminate("chmod() on outside temp directory ({}) failed, errno: {}", dir.string(), errno); }
        }

        /// @brief Get default mount() flags parameter common for all types of rules.
        unsigned long default_flags()
        {
            unsigned long flags = 0;
            if(!rule_->rw())      { flags |= MS_RDONLY; }
            if(rule_->noexec())   { flags |= MS_NOEXEC; }            
            if(!rule_->dev())     { flags |= MS_NODEV; }            
            
            return flags;
        }
    };
    
    /// @brief Manager class for handling environment variables passed to the tasks.
    class env_manager
    {
    public:
        env_manager()
        {}

        /// @brief 
        /// @param config Environment node of the config.
        env_manager(const config::env_config& config) : config_(&config)
        {}
        
        /// @brief Prepares if necessary and returns the envp array for execve() 
        char** get_envp()
        {
            if(!env_ || !envp_) 
            {
                env_ = prepare_env();
                envp_ = to_envp(env_.value());
            }
            return envp_.value().data();
        }

    private:
        /// @brief Pointer to the env_config configuration node.
        const config::env_config* config_;
        
        /// @brief Vector storing the environment so that we can safely convert it
        /// to vector<char*>.
        std::optional<std::vector<std::string>> env_;

        /// @brief Vector storing the envp vector of char* to be passed to execve().
        std::optional<std::vector<char*>> envp_;

        /// @brief Parse the environment rules into a vector of "name=value" environment variable assignments.
        std::vector<std::string> prepare_env()
        {
            std::unordered_map<std::string, std::string> env_map;
            
            /// First copy whole environment if specified in the config.
            /// Then, values can be added/overwritten by other rules.
            if(config_->inherit_all())
            {
                for (char **env = environ; *env; ++env)
                {
                    std::string entry(*env);
                    auto eq = entry.find('=');
                    if (eq != std::string::npos) 
                    {
                        auto name = entry.substr(0, eq);
                        auto value = entry.substr(eq + 1); 
                        env_map[name] = value; 
                    }
                } 
            }
            
            for(auto&& r : config_->rules())
            {
                if(r.inherited_var())
                {
                    auto& name = r.inherited_var().value();
                    auto value = std::getenv(name.c_str());
                    if(value) { env_map[name] = value; }
                }
                else if (r.name_value_pair())
                {
                    auto& [name, value] = r.name_value_pair().value();
                    env_map[name] = value;
                }  
            }

            std::vector<std::string> env;
            for(const auto& [name, value] : env_map)
            {
                env.emplace_back(name + "=" + value);
            }
                
            return env;
        }
        
        /// @brief Convert a vector<string> to null terminated vector<char*>
        /// @return We pass RV.data() to execve() as the envp parameter.
        std::vector<char*> to_envp(std::vector<std::string>& strings)
        {
            std::vector<char*> envp;
            for(auto&& str : strings)
            {
                envp.emplace_back(str.data());
            }
            envp.emplace_back(nullptr);
            return envp;
        }
    };

    /// @brief Manager class for the box directory tree.
    class box_fs_manager
    {
    public:
        /// @brief 
        /// @param fs_config Reference to box fs node of configuration.
        /// @param credentials Reference to credentials manager to get box UID/GID for chown().
        box_fs_manager(const config::box_fs_config& fs_config, credentials::proxy_credentials_manager& credentials) :   fs_config_(&fs_config),
                                                                                                                        credentials_(&credentials)
        {}
        
        /// @brief Create the box directory tree.
        void run()
        {
            construct_box_tree();
        }

    private:
        /// @brief Box fs node of configuration.
        const config::box_fs_config* fs_config_;
        
        /// @brief Credentials manager to get box UID/GID for chown().
        credentials::proxy_credentials_manager* credentials_;

        /// @brief Create the box directory tree.
        void construct_box_tree()
        {
            /// We create mount points for all rules first (TODO: security trick from Isolate)
            create_mount_points();
            apply_rules();
        }
        
        /// @brief Apply all rules. Assumes all needed directories were created by create_mount_points().
        void apply_rules()
        {
            if(fs_config_->use_default_rules())
            {
                for(auto&& rule : fs_config_->default_rules())
                {
                    dir_rule_supervisor drs(rule, *credentials_);
                    drs.apply();
                }
            } 

            for(auto&& rule : fs_config_->rules())
            {
                dir_rule_supervisor drs(rule, *credentials_);
                drs.apply();
            }
        }
        
        /// @brief Create the directories for all rules first, detects and remembers created dummy directories.
        void create_mount_points()
        {
            std::vector<std::tuple<fs::path,fs::path>> mount_points;
            if(fs_config_->use_default_rules())
            {
                for(auto&& rule : fs_config_->default_rules())
                {
                    create_directories_for_rule(mount_points, rule);
                }
            }

            for(auto&& rule : fs_config_->rules())
            {
                create_directories_for_rule(mount_points, rule);
            }
        }

        /// @brief Create directories of a single rule. Detects and remembers created dummy directories.
        /// @param mount_points Previous mount points inside the box that escape to the outside.
        /// @param rule Config of the rule.
        void create_directories_for_rule(std::vector<std::tuple<fs::path, fs::path>>& mount_points, const config::dir_rule& rule)
        {
            dir_rule_supervisor drs(rule, *credentials_);
            if(rule_escapes_box(rule))
            {
                auto inner = credentials_->box_root() / rule.in_dir();
                auto dummy_dir = potential_dummy_dir(mount_points, inner);
                if(dummy_dir) 
                {
                    logs::debug("Potential dummy dir: {}", dummy_dir.value().string()); 
                    drs.add_dummy_dir(dummy_dir.value()); 
                }
                mount_points.emplace_back(std::tuple(inner, rule.out_dir()));
            }
            
            drs.create_directories();
        }

        /// @brief Detect if a new mount inside the box needs a dummy directory and return the path.
        /// @param mount_points Previous mount points inside the box that escape to the outside.
        /// @param new_path The inner path of the current rule.
        /// @return Outside path of the dummy directory, if it needs to be created. No value otherwise.
        static std::optional<fs::path> potential_dummy_dir(const std::vector<std::tuple<fs::path, fs::path>>& mount_points, const fs::path& new_path)
        {
            fs::path longest_prefix;
            fs::path underlying_dir;

            for(auto&& [inner, outer] : mount_points)
            {
                if(file_utils::is_prefix(inner, new_path) && inner.string().length() > longest_prefix.string().length())
                { 
                    longest_prefix = inner;
                    underlying_dir = outer; 
                }
            }
            auto dummy_dir = underlying_dir / new_path.lexically_relative(longest_prefix);
            std::optional<fs::path> res;
            if(dummy_dir.string() != "") res = dummy_dir;
            return res;           
        }
        
        /// @brief Check if a rule escapes outside the box.
        /// @param rule 
        /// @return True, unless the rule is a mount of a filesystem.
        static bool rule_escapes_box(const config::dir_rule& rule)
        {
            return !rule.fs(); 
        }
    };
}

#endif