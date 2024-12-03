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
            mount_pivot_dir();
            mount_cgroup();
        }
    private:
        void mount_pivot_dir()
        {
            if(mount("/alpine", "/alpine", nullptr, MS_REC | MS_BIND, nullptr))
                terminate("failed to bind mount the pivot directory, errno: {}", errno);
        }
        void make_root_rslave()
        {
            if(mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr))
                terminate("failed to change propagation type of root mount, errno: {}", errno);
        }
        void mount_cgroup()
        {
            auto u = umount("/sys/fs/cgroup");
            if(u) terminate("failed to unmount cgroup filesystem, errno: {}", errno);
            auto m = mount("none", "/alpine/sys/fs/cgroup", "cgroup2", 0, nullptr);
            if( m) terminate("failed to remount cgroup2 filesystem, errno: {}", errno);
        }
    };

}


#endif