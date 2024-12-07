#ifndef CONTAINER_ENV
#define CONTAINER_ENV

#include <sys/mount.h>
#include <errno.h>
#include "config.hpp"
#include "terminate.hpp"
#include "cgrps.hpp"

namespace env
{
    class root_env_mngr
    {

    };
    
    class proxy_mount_manager
    {
    public:
        proxy_mount_manager(config::proxy_config* proxy_config) : proxy_config_(proxy_config)
        {}
        void run()
        {
            make_root_rslave();
            mount_pivot_dir();
            mount_cgroup();
        }
    private:
        config::proxy_config* proxy_config_;

        void mount_pivot_dir()
        {
            auto& chroot_dir = proxy_config_->get_chroot_dir().value();
            if(mount(chroot_dir.c_str(), chroot_dir.c_str(), nullptr, MS_REC | MS_BIND, nullptr))
                terminate("failed to bind mount the pivot directory, errno: {}", errno);
        }
        void make_root_rslave()
        {
            if(mount(nullptr, "/", nullptr, MS_SLAVE | MS_REC, nullptr))
                terminate("failed to change propagation type of root mount, errno: {}", errno);
        }
        void mount_cgroup()
        {
            if(umount(cgroup::ROOT_CG_PATH().c_str()))
                terminate("failed to unmount cgroup filesystem, errno: {}", errno);
            
            auto& chroot_dir = proxy_config_->get_chroot_dir().value();
            if(mount("none", (chroot_dir / cgroup::ROOT_CG_PATH().relative_path()).c_str(), "cgroup2", 0, nullptr))
                terminate("failed to remount cgroup2 filesystem, errno: {}", errno);
        }
    };

}


#endif