#ifndef CONTAINER_ENV
#define CONTAINER_ENV

#include <sys/mount.h>
#include <errno.h>
#include "config.hpp"
#include "terminate.hpp"

namespace env
{
    class root_env_mngr
    {

    };
    
    class proxy_mount_manager
    {
    public:
        void mount_all()
        {
            make_root_rslave();
            mount_cgroup();
        }
    private:
        void make_root_rslave()
        {
            if(mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr))
                terminate("failed to change propagation type of root mount, errno: {}", errno);
        }
        void mount_cgroup()
        {
            auto u = umount("/sys/fs/cgroup");
            if(u) terminate("failed to unmount cgroup filesystem, errno: {}", errno);
            auto m = mount("none", "/sys/fs/cgroup", "cgroup2", 0, nullptr);
            if(u || m) terminate("failed to remount cgroup2 filesystem, errno: {}", errno);
        }
    };

}


#endif