#ifndef CREDENTIALS
#define CREDENTIALS

#include <random>
#include <filesystem>

#include <sys/types.h>
#include <grp.h>
#include <unistd.h>
#include "config.hpp"


namespace credentials
{
    namespace fs = std::filesystem;

    ///  @class root_credentials_manager
    ///  @brief Manager class responsible for assigning credentials (box_id, UID/GID) used by the box.
    ///  @details
    class root_credentials_manager
    {
    public:
        root_credentials_manager() {}
        
        /// @brief
        /// @param config
        root_credentials_manager(const config::credentials_config& config) : config_(&config)
        {}
        
        /// @brief Assign box_id, UID and GID that will be later used by the box.
        void run()
        {
            orig_uid_ = getuid();
            orig_gid_ = getgid();
            assign_box_ids();
            box_root_ = assign_box_root(box_id_);
            box_cgroup_ = assign_box_cgroup(box_id_);
        }
            
        /// @brief Getter for the original UID.
        uid_t orig_uid() const
        {
            return orig_uid_;
        }

        /// @brief Getter for the original GID.
        gid_t orig_gid() const
        {
            return orig_gid_;
        }
        
        /// @brief Getter for the assigned box_id.
        uid_t box_id() const
        {
            return box_id_;
        }
        
        /// @brief Getter for the assigned box_uid.
        uid_t box_uid() const
        {
            return box_uid_;
        }

        /// @brief Getter for the assigned box_gid.
        uid_t box_gid() const
        {
            return box_gid_;
        }
        
        /// @brief Getter for the path of the root cgroup of this instance.
        const fs::path& instance_cgroup() const
        {
            return box_cgroup_;
        }

        /// @brief Getter for the root directory of this instance.
        const fs::path& box_root() const
        {
            return box_root_;
        }
    private:
        /// @brief Credentials configuration node.
        const config::credentials_config* config_;

        /// @brief See section Credentials in the readme.
        uid_t box_id_;        

        /// @brief UID that the box will use (Used for example for file ownership).
        uid_t box_uid_;        
        
        /// @brief GID that the box will use (Used for example for file ownership).
        uid_t box_gid_;        

        /// @brief The original UID as which this instance was launched.
        uid_t orig_uid_;

        /// @brief The original GID as which this instance was launched.
        gid_t orig_gid_;

        /// @brief Root directory for the filesystem of this instance.
        fs::path box_root_;
        
        /// @brief Root cgroup for this instance.
        fs::path box_cgroup_;

        /// @brief Start of the range from which box UID and GID are assigned.
        static constexpr uid_t box_uid_range_start_ = 60000;
        
        /// @brief Reserves a box_id and assigns UID/GID by adding the id and box_uid_range_start_.
        /// @details Currently the box_id is randomly assigned, a daemon keeper process that assigns IDs is planned.
        void assign_box_ids()
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uid_t> dist(1,5000);

            box_id_ = dist(gen);
            box_uid_ = box_uid_range_start_ + box_id_;
            box_gid_ = box_uid_;
        }
        
        /// @brief Assign a root directory for the filesystem of this instance.
        /// @param box_id box_id acquired in assign_box_ids().
        fs::path assign_box_root(uid_t box_id)
        {
            auto idf = config_->instance_name() ? config_->instance_name().value() : std::to_string(box_id);
            return config_->boxes_dir() / fs::path(idf);
        }
        
        /// @brief Assign a root cgroup for this instance.
        /// @param box_id box_id acquired in assign_box_ids().
        fs::path assign_box_cgroup(uid_t box_id)
        {
            auto idf = config_->instance_name() ? config_->instance_name().value() : std::to_string(box_id);
            return config_->boxes_cgroup() / idf;
        }
    };

    /// @brief Manager class responsible for accessing and switching credentials (UID/GID).
    class proxy_credentials_manager
    {
    public:
        proxy_credentials_manager() {}
        
        /// @brief Constructor
        /// @param root_manager Reference to the corresponding root class that assigns credentials.
        proxy_credentials_manager(const root_credentials_manager& root_manager) : credentials_root_(&root_manager)
        {}
        
        /// @brief Getter for the assigned box_uid.
        uid_t box_uid() const
        {
            return credentials_root_->box_uid();
        }

        /// @brief Getter for the assigned box_gid.
        uid_t box_gid() const
        {
            return credentials_root_->box_gid();
        }

        /// @brief Switch back to the UID/GID as which this instance was launched.
        /// @details Switches real, effective, and saved-set UID and GID. 
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

        /// @brief Switch credentials (UID and GID) to values assigned to the box.
        /// @details Switches real, effective, and saved-set UID and GID. 
        void switch_to_box()
        {
            auto box_gid = credentials_root_->box_gid();
            if(setresgid(box_gid, box_gid, box_gid) < 0)
                { terminate("Couldn't switch to box GID, errno: {}", errno); }

            /// TODO: Find out why setgroups is necessary.
            if(setgroups(0, NULL) < 0)
                { terminate("Setgroups failed, errno: {}", errno); }

            auto box_uid = credentials_root_->box_uid();
            if(setresuid(box_uid, box_uid, box_uid) < 0)
                { terminate("Couldn't switch to box UID, errno: {}", errno); }
        }
        
        /// @brief Getter for box root directory assigned by the root credentials manager class.
        const fs::path& box_root() const
        {
            return credentials_root_->box_root();
        }

        /// @brief Getter for sandbox root cgroup assigned by the root credentials manager class.
        const fs::path& box_cgroup() const
        {
            return credentials_root_->instance_cgroup();
        }

    private:
        /// @brief Corresponding root class that assigns credentials.
        const root_credentials_manager* credentials_root_;
    };
}

#endif