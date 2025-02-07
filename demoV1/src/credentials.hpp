#ifndef CREDENTIALS
#define CREDENTIALS

#include <unistd.h>
#include "config.hpp"

namespace credentials
{
    class root_credentials_manager
    {
    public:
        root_credentials_manager() {}
        root_credentials_manager(const config::credentials_config& config) : config_(&config)
        {
            orig_uid_ = getuid();
            orig_gid_ = getgid();
        }

    private:
        const config::credentials_config* config_;
        uid_t box_id_;        
        uid_t orig_uid_;
        gid_t orig_gid_;
        
        uid_t get_box_id()
        {
            return 1;
        }
    };

    class proxy_credentials_manager
    {
    public:
        proxy_credentials_manager() {}
        proxy_credentials_manager(const config::credentials_config& config) : config_(&config)
        {

        }
        void change_to_user()
        {
            
        }

        void change_to_box()
        {

        }

    private:
        const config::credentials_config* config_;
        const root_credentials_manager* root_;
    };

    
    
}

#endif