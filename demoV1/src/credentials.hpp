#ifndef CREDENTIALS
#define CREDENTIALS

#include <sys/types.h>
#include <grp.h>
#include <unistd.h>
#include "config.hpp"

namespace credentials
{
    class root_credentials_manager
    {
    public:
        root_credentials_manager() {}
        root_credentials_manager(const config::credentials_config& config) : config_(&config)
        {}
        
        void run()
        {
            orig_uid_ = getuid();
            orig_gid_ = getgid();
            box_id_ = assign_box_id();
        }
            
        uid_t orig_uid() const
        {
            return orig_uid_;
        }

        gid_t orig_gid() const
        {
            return orig_gid_;
        }
        
        uid_t box_id() const
        {
            return box_id_;
        }
        
        uid_t box_uid() const
        {
            return box_uid_range_start_ + box_id_;
        }

        uid_t box_gid() const
        {
            return box_gid_range_start_ + box_id_;
        }

    private:
        const config::credentials_config* config_;

        uid_t box_id_;        

        uid_t orig_uid_;
        gid_t orig_gid_;
        
        uid_t box_uid_range_start_ = 60000;
        gid_t box_gid_range_start_ = 400000;
        size_t box_id_range_ = 1000;
        
        uid_t assign_box_id()
        {
            uid_t box_id = 1;
            
            if(box_id > box_id_range_)
                { terminate("Generated box_id is out of range"); }

            return box_id;
        }
    };

    class proxy_credentials_manager
    {
    public:
        proxy_credentials_manager() {}
        proxy_credentials_manager(const root_credentials_manager& root_manager) : credentials_root_(&root_manager)
        {}
        
        uid_t box_uid() const
        {
            return credentials_root_->box_uid();
        }

        uid_t box_gid() const
        {
            return credentials_root_->box_gid();
        }

        void switch_to_user()
        {
            auto orig_gid = credentials_root_->orig_gid();
            if(setresgid(orig_gid, orig_gid, orig_gid) < 0)
                { terminate("Couldn't switch to original GID, errno: {}", errno); }
            
            if(setgroups(0, NULL) < 0)
                { terminate("Setgroups failed, errno: {}", errno); }

            auto orig_uid = credentials_root_->orig_uid();
            if(setresuid(orig_uid, orig_uid, orig_uid) < 0)
                { terminate("Couldn't switch to original UID, errno: {}", errno); }
        }

        void switch_to_box()
        {
            auto box_gid = credentials_root_->box_gid();
            if(setresgid(box_gid, box_gid, box_gid) < 0)
                { terminate("Couldn't switch to box GID, errno: {}", errno); }

            if(setgroups(0, NULL) < 0)
                { terminate("Setgroups failed, errno: {}", errno); }

            auto box_uid = credentials_root_->box_uid();
            if(setresuid(box_uid, box_uid, box_uid) < 0)
                { terminate("Couldn't switch to box UID, errno: {}", errno); }
        }

    private:
        const root_credentials_manager* credentials_root_;
    };
}

#endif