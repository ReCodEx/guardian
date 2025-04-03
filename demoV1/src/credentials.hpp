#ifndef CREDENTIALS
#define CREDENTIALS

#include <random>

#include <sys/types.h>
#include <grp.h>
#include <unistd.h>
#include "config.hpp"


namespace credentials
{
    /**
     * @class root_credentials_manager
     * @brief Responsible for assigning credentials (box_id, UID/GID) used by the box.
     * 
     * @details
     */
    class root_credentials_manager
    {
    public:
        root_credentials_manager() {}
        
        /// @brief
        /// @param config
        root_credentials_manager(const config::credentials_config& config) : config_(&config)
        {}
        
        /**
         * @brief Assign box_id, UID and GID that will be later used by the box.
         */
        void run()
        {
            orig_uid_ = getuid();
            orig_gid_ = getgid();
            assign_box_ids();
        }
            
        /**
         * @brief Getter for the original UID.
         */
        uid_t orig_uid() const
        {
            return orig_uid_;
        }

        /**
         * @brief Getter for the original GID.
         */
        gid_t orig_gid() const
        {
            return orig_gid_;
        }
        
        /**
         * @brief Getter for the assigned box_id.
         */
        uid_t box_id() const
        {
            return box_id_;
        }
        
        /**
         * @brief Getter for the assigned box_uid.
         */
        uid_t box_uid() const
        {
            return box_uid_;
        }

        /**
         * @brief Getter for the assigned box_gid.
         */
        uid_t box_gid() const
        {
            return box_gid_;
        }

    private:
        /// @brief  Pointer to the config class
        const config::credentials_config* config_;

        /// @brief See section Credentials in the readme.
        uid_t box_id_;        

        /// @brief See section Credentials in the readme.
        uid_t box_uid_;        
        
        /// @brief See section Credentials in the readme.
        uid_t box_gid_;        

        /// @brief The original UID as which the container was launched.
        uid_t orig_uid_;

        /// @brief The original GID as which the container was launched.
        gid_t orig_gid_;
        
        static constexpr uid_t box_uid_range_start_ = 60000;
        
        /**
         * @brief Reserves a box_id and assigns UID/GID.
         * @details Currently randomly assigned, a daemon keeper process that assigns IDs is planned.
         */
        void assign_box_ids()
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uid_t> dist(1,5000);

            box_id_ = dist(gen);
            box_uid_ = box_uid_range_start_ + box_id_;
            box_gid_ = box_uid_;
        }
    };

    class proxy_credentials_manager
    {
    public:
        proxy_credentials_manager() {}
        
        /**
         * @brief Constructor
         * @param root_manager Reference to the root class storing assigned credentials.
         */
        proxy_credentials_manager(const root_credentials_manager& root_manager) : credentials_root_(&root_manager)
        {}
        
        /**
         * @brief Getter for the assigned box_uid.
         */
        uid_t box_uid() const
        {
            return credentials_root_->box_uid();
        }

        /**
         * @brief Getter for the assigned box_gid.
         */
        uid_t box_gid() const
        {
            return credentials_root_->box_gid();
        }

        /**
         * @brief Switch back to the original UID/GID.
         * 
         * @details Switches real, effective, and saved-set UID and GID. 
         */
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

        /**
         * @brief Switch credentials to the assigned values for the box.
         * 
         * @details Switches real, effective, and saved-set UID and GID. 
         */
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

    private:
        /// @brief Pointer to the root class storing assigned credentials.
        const root_credentials_manager* credentials_root_;
    };
}

#endif