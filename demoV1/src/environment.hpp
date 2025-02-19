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

            auto& box_root = proxy_config_->box_root().value();
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
            
            auto& box_root = proxy_config_->box_root().value();
            if(mount("none", (box_root / cgroup::ROOT_CG_PATH().relative_path()).c_str(), "cgroup2", 0, nullptr))
                { terminate("failed to remount cgroup2 filesystem, errno: {}", errno); }
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
            if(fs_config_->box_root().has_value()) { construct_box_fs(); }
        }
        

    private:
        const config::box_fs_config* fs_config_;
        credentials::proxy_credentials_manager* credentials_;

        void construct_box_fs()
        {
            auto& rules = fs_config_->rules();
            auto& box_root = fs_config_->box_root();

            if(fs_config_->use_default_rules())
            {
                for(auto&& rule : fs_config_->default_rules())
                {
                    apply_rule(rule);
                }
            } 

            for(auto&& rule : rules)
            {
                apply_rule(rule);
            }
        }

        void apply_rule(const config::dir_rule& rule)
        {
            fs::path in(fs_config_->box_root().value() / rule.in_dir());
            fs::path out = fs::path("/") / (rule.out_dir() ? rule.out_dir().value() : rule.in_dir());
            auto flags = default_flags(rule);
            create_inner_dir(in);
            
            if(rule.fs())
            {
                if(mount("none", in.c_str(), out.c_str() + 1, flags, "") < 0)
                    { terminate("Mount failed for directory rule: {}, errno: {}", rule.string(), errno); }
                
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
                if(!rule.norec()) { flags |= MS_REC; }
/*                 file_utils::list_directory(fs::path("/home/simonkurz/mff")); */
                if( mount(out.c_str(), in.c_str(), "none", flags, "") < 0 ||
                    mount(out.c_str(), in.c_str(), "none", MS_REMOUNT | flags, "") < 0)
                    { terminate("Mount failed for directory rule: {}, errno: {}", rule.string(), errno); }
            }
        }
        
        void create_inner_dir(const fs::path& dir)
        {
            if(fs::is_directory(dir))
            { 
                //terminate("Box inner directory already exists: {}", dir.string()); }
            }
            else
            { fs::create_directory(dir); } 
            
/*             if(chown(dir.c_str(), credentials_->box_uid(), credentials_->box_gid()) < 0)
                { terminate("chown on directory inside box failed, errno: {}", errno); } */
        }

        static unsigned long default_flags(const config::dir_rule& rule)
        {
            unsigned long flags = 0;
            if(!rule.rw())      { flags |= MS_RDONLY; }
            if(rule.noexec())   { flags |= MS_NOEXEC; }            
            if(!rule.dev())     { flags |= MS_NODEV; }            
            
            return flags;
        }
    };
}


#endif