#ifndef QUOTA
#define QUOTA

#include <fcntl.h>
#include <linux/dqblk_xfs.h>
#include <linux/quota.h>
#include <sys/quota.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>

#include "logs.hpp"
#include "terminate.hpp"

/// @brief The box's disk quota: a user quota on the box-tree filesystem, kept
/// by the kernel against `box_uid`.
namespace quota {
    namespace fs = std::filesystem;

    /// @brief The caps of a disk quota. A 0 leaves that cap unlimited — the
    /// kernel's own reading of a 0 limit
    struct limits {
        std::uint64_t blocks = 0;  ///< In 1 KiB quota blocks, not fs blocks.
        std::uint64_t inodes = 0;  ///< Number of files, directories, ...

        /// @brief Whether no cap is asked for at all.
        bool unlimited() const { return blocks == 0 && inodes == 0; }
    };

    /// @brief The quota blocks that hold @p bytes, rounded up so a small
    /// non-zero cap never becomes 0, i.e. unlimited.
    constexpr std::uint64_t blocks_for_bytes(std::uint64_t bytes) {
        return (bytes + QIF_DQBLKSIZE - 1) / QIF_DQBLKSIZE;
    }

    namespace detail {
        /// @brief quotactl_fd(2) (Linux 5.14, no glibc wrapper): addresses the
        /// filesystem by an fd on any directory in it rather than by its device
        /// node, as Isolate does.
        inline int quotactl_fd(int fd, int cmd, uid_t id, void* addr) {
            return static_cast<int>(
                syscall(__NR_quotactl_fd, fd, cmd, id, addr));
        }

        /// @brief The user-quota state (`FS_QUOTA_UDQ_ACCT` = usage is kept,
        /// `FS_QUOTA_UDQ_ENFD` = limits are enforced) of the filesystem @p fd
        /// is on; 0 when it keeps no quotas at all.
        inline std::uint16_t user_quota_state(int fd, const fs::path& dir) {
            fs_quota_stat st{};
            if (quotactl_fd(fd, QCMD(Q_XGETQSTAT, USRQUOTA), 0, &st) < 0) {
                // ENOSYS: no quota support, or no quota type turned on.
                if (errno == ENOSYS) {
                    return 0;
                }
                terminate("Cannot read the disk quota state of {}: errno {}",
                          dir.string(), errno);
            }
            return st.qs_flags & (FS_QUOTA_UDQ_ACCT | FS_QUOTA_UDQ_ENFD);
        }
    }  // namespace detail

    /// @brief Set @p uid's disk quota on the filesystem holding @p dir to
    /// @p caps, replacing whatever was set before (soft limit = hard limit,
    /// as in Isolate).
    /// @details A cap that is asked for is enforced or the phase fails: this
    /// terminate()s when the filesystem keeps no user quota (Isolate's
    /// "quotas have not been enabled") or records the limits without enforcing
    /// them (ext4's `quota` feature mounted without `usrquota`, where the set
    /// itself succeeds). No cap at all (@ref limits::unlimited) clears any
    /// stored one, and needs no quota filesystem: one that keeps no user quota
    /// has nothing to clear.
    inline void set(const fs::path& dir, uid_t uid, limits caps) {
        const int fd = open(dir.c_str(), O_DIRECTORY | O_PATH | O_CLOEXEC);
        if (fd < 0) {
            terminate("Cannot open {} to set its disk quota: errno {}",
                      dir.string(), errno);
        }

        const std::uint16_t state = detail::user_quota_state(fd, dir);
        if (caps.unlimited() && !(state & FS_QUOTA_UDQ_ACCT)) {
            close(fd);
            return;
        }
        if (!(state & FS_QUOTA_UDQ_ACCT)) {
            terminate(
                "Cannot set disk quota: quotas have not been enabled for the "
                "filesystem of {}",
                dir.string());
        }
        if (!caps.unlimited() && !(state & FS_QUOTA_UDQ_ENFD)) {
            terminate(
                "Cannot set disk quota: the filesystem of {} records user "
                "quotas but does not enforce them (mount it with usrquota)",
                dir.string());
        }

        struct dqblk dq = {
            .dqb_bhardlimit = caps.blocks,
            .dqb_bsoftlimit = caps.blocks,
            .dqb_ihardlimit = caps.inodes,
            .dqb_isoftlimit = caps.inodes,
            .dqb_valid = QIF_LIMITS,
        };
        if (detail::quotactl_fd(fd, QCMD(Q_SETQUOTA, USRQUOTA), uid, &dq) < 0) {
            terminate("Cannot set disk quota on {}: errno {}", dir.string(),
                      errno);
        }
        close(fd);
        logs::debug("Disk quota of uid {} on {}: {} blocks, {} inodes", uid,
                    dir.string(), caps.blocks, caps.inodes);
    }
}  // namespace quota

#endif
