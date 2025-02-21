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

    class proxy_mount_manager
    {
    public:
        proxy_mount_manager(const config::proxy_config* proxy_config) : proxy_config_(proxy_config)
        {}

        void run()
        {
            make_root_rslave();
            mount_pivot_dir();
            mount_cgroup();
        }
    private:
        const config::proxy_config* proxy_config_;

        void mount_pivot_dir()
        {
            //the directory that we pivot_root to has to be a mount point

            auto& box_root = proxy_config_->box_root();
            if(mount(box_root.c_str(), box_root.c_str(), nullptr, MS_REC | MS_BIND, nullptr))
                { terminate("failed to bind mount the pivot directory, errno: {}", errno); }
        }
        
        void make_root_rslave()
        {
            //don't propagate mount events to other namespaces
            
            if(mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr))
                { terminate("failed to change propagation type of root mount, errno: {}", errno); }
        }

        void mount_cgroup()
        {
            //remount cgroup filesystem into the box, questionable for security but simplifies delegation
            // of responsibilities

            if(umount(cgroup::ROOT_CG_PATH().c_str()))
                { terminate("failed to unmount cgroup filesystem, errno: {}", errno); }
            
            auto& box_root = proxy_config_->box_root();
            if(mount("none", (box_root / cgroup::ROOT_CG_PATH().relative_path()).c_str(), "cgroup2", 0, nullptr))
                { terminate("failed to remount cgroup2 filesystem, errno: {}", errno); }
        }
    };
    
    class dir_rule_supervisor
    {
    public:
        dir_rule_supervisor(const config::dir_rule& rule, const credentials::proxy_credentials_manager& creds) : rule_(&rule), credentials_(&creds)
        {}

        void apply()
        {
            auto& in = rule_->in_dir();
            auto& out = rule_->out_dir();
            auto flags = default_flags();

            if(rule_->fs())
            {
                if(mount("none", in.c_str(), out.c_str() + 1, flags, "") < 0)
                    { terminate("Mount failed for directory rule: {}, errno: {}", rule_->string(), errno); }
                
                // If we are mounting procfs, add hidepid=2, so that only the processes
	            // of the same user are visible. This has to be done as a remount.
                if(in.c_str() == "proc")
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
        
        void create_directories()
        {
            create_inner_dir();
            if(rule_->tmp()) { create_outer_temp_dir(); }
            if(dummy_dir_) { create_dummy_dir(); }
        }
        
        void add_dummy_dir(const fs::path& dir)
        {
            dummy_dir_ = dir;
        }

    private:
        const config::dir_rule* rule_;
        const credentials::proxy_credentials_manager* credentials_;
        
        std::optional<fs::path> dummy_dir_;
        
        void create_inner_dir()
        {
            logs::debug("Checking for inner dir {}", rule_->in_dir().string());
            create_dir(rule_->in_dir());
        }
    
        void create_outer_temp_dir()
        {
            create_dir(rule_->out_dir());
        }

        void create_dummy_dir()
        {
            logs::debug("Checking for dummy dir: {}", dummy_dir_.value().string());
            create_dir(dummy_dir_.value());
        }
        
        void create_dir(const fs::path& dir)
        {
            if(!fs::is_directory(dir))
            { 
                logs::debug("Creating directory: {}",dir.string());

                if(!fs::create_directory(dir))
                    { terminate("Failed to create outside directory ({})", dir.string()); } 
            } 
            
            if(chown(dir.c_str(), credentials_->box_uid(), credentials_->box_gid()) < 0)
                { terminate("chown() on outside temp directory ({}) failed, errno: {}", dir.string(), errno); }
            
            if(chmod(dir.c_str(), 0777) < 0)
                { terminate("chmod() on outside temp directory ({}) failed, errno: {}", dir.string(), errno); }
        }

        unsigned long default_flags()
        {
            unsigned long flags = 0;
            if(!rule_->rw())      { flags |= MS_RDONLY; }
            if(rule_->noexec())   { flags |= MS_NOEXEC; }            
            if(!rule_->dev())     { flags |= MS_NODEV; }            
            
            return flags;
        }
    };
    
    class box_fs_manager
    {
    public:
        box_fs_manager(const config::box_fs_config& fs_config, credentials::proxy_credentials_manager& credentials) :   fs_config_(&fs_config),
                                                                                                                        credentials_(&credentials)
        {}
        
        void run()
        {
            construct_box_fs();
        }

    private:
        const config::box_fs_config* fs_config_;
        credentials::proxy_credentials_manager* credentials_;

        void construct_box_fs()
        {
            create_mount_points();
            apply_rules();
        }
        
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

        void create_directories_for_rule(std::vector<std::tuple<fs::path, fs::path>>& mount_points, const config::dir_rule& rule)
        {
            dir_rule_supervisor drs(rule, *credentials_);
            if(rule_escapes_box(rule))
            {
                auto dummy_dir = potential_dummy_dir(mount_points, rule.in_dir());
                if(dummy_dir) 
                {
                    logs::debug("Potential dummy dir: {}", dummy_dir.value().string()); 
                    drs.add_dummy_dir(dummy_dir.value()); 
                }
                mount_points.emplace_back(std::tuple(rule.in_dir(), rule.out_dir()));
            }
            
            drs.create_directories();
        }

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
        
        static bool rule_escapes_box(const config::dir_rule& rule)
        {
            return !(rule.dev() || rule.fs()); 
        }
    };
}

#endif