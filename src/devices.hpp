#ifndef DEVICES
#define DEVICES

#include <string>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <mntent.h>


#include "logs.hpp"
#include "terminate.hpp"

namespace devices
{
    namespace fs = std::filesystem;

    /// @brief Find the device (filesystem) where a directory resides.
    /// @note Needed for setting up disk quota.
    /// @param p Path to the directory.
    /// @return Name of the device.
    inline std::string find_device_for_dir(const fs::path& p)
    {
        struct stat st;
        if (stat(p.c_str(), &st) == -1) 
            { terminate("stat() failed when finding device for cwd, errno: {}", errno); }
        
        dev_t target_dev = st.st_dev;

        std::ifstream mounts("/proc/mounts");
        if (!mounts)
            { terminate("Failed to open /proc/mounts when finding device for cwd, errno: {}", errno); } 

        std::string line;
        while (getline(mounts, line))
        {
            std::istringstream iss(line);
            // /proc/mounts format: fsname mountpoint fstype options dump pass
            std::string fsname, mnt_dir, fstype, options;
            int dump, pass;
            if (!(iss >> fsname >> mnt_dir >> fstype >> options >> dump >> pass)) { continue; } // Skip malformed lines.

            struct stat mnt_stat;
            if (stat(mnt_dir.c_str(), &mnt_stat) != 0)  { continue; } // Skip if unable to stat the mount point. 
            if (mnt_stat.st_dev == target_dev)          { return fsname; } // Found matching device.
        }
        terminate("Device of cwd not found");
        return std::string();
    }
}

#endif