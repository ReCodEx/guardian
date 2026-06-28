#ifndef LOCK
#define LOCK

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "terminate.hpp"

/// @brief Per-box advisory lock shared by all three compatibility phases.
/// @details Following Isolate, every `--init` / `--run` / `--cleanup`
/// invocation takes an exclusive, non-blocking `flock` on a small record file
/// keyed by `--box-id`, so a second invocation on the same box fails rather
/// than corrupting a box in use. The record carries a magic number and an
/// `is_initialized` bit, letting `--run` refuse a box that was never `--init`'d
/// (ADR 0001).
///
/// The lock lives on tmpfs at `/run/isolate_boxes/locks/<box-id>`, deliberately
/// outside the persistent box tree under `/isolate_boxes`: `--cleanup`
/// `rm -rf`s the box dir, so the lock must survive that, and a stale lock after
/// reboot is harmless (tmpfs is empty). `--cleanup` `ftruncate`s the lock to
/// zero — clearing the magic, marking it uninitialized — rather than
/// `unlink`ing it, to avoid a re-open/re-create race on the same path.
namespace lock {
    namespace fs = std::filesystem;

    namespace defaults {
        /// @brief tmpfs directory holding one record file per box-id.
        constexpr auto LOCKS_DIR = "/run/isolate_boxes/locks";
    }  // namespace defaults

    /// @brief On-disk lock record. A box is considered initialized only when a
    /// full record with the expected magic and a set flag is present; an empty
    /// (freshly created or `ftruncate`d) file therefore reads as uninitialized.
    struct lock_record {
        std::uint32_t magic = 0;
        std::uint8_t initialized = 0;
    };

    /// @brief Distinguishes a valid record from a zeroed / garbage file.
    constexpr std::uint32_t LOCK_MAGIC = 0x150'1A7E;  // "ISO-LATE"-ish.

    /// @brief RAII holder of a box's exclusive lock.
    /// @details The lock is acquired in the constructor and released when the
    /// object (and thus the file descriptor) is destroyed; the kernel also
    /// drops it on process exit. Non-copyable.
    class box_lock {
       public:
        /// @brief Open (creating if absent) the lock for @p box_id and take a
        /// non-blocking exclusive `flock`. Terminates if the box is already
        /// locked by another invocation, or on any I/O error.
        box_lock(std::size_t box_id,
                 const fs::path& locks_dir = defaults::LOCKS_DIR) {
            std::error_code ec;
            fs::create_directories(locks_dir, ec);
            if (ec) {
                terminate("Cannot create locks directory {}: {}",
                          locks_dir.string(), ec.message());
            }

            path_ = locks_dir / std::to_string(box_id);
            fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (fd_ < 0) {
                terminate("Cannot open box lock {}: errno {}", path_.string(),
                          errno);
            }

            if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
                if (errno == EWOULDBLOCK) {
                    terminate("Box {} is already locked by another invocation",
                              box_id);
                }
                terminate("Cannot flock box lock {}: errno {}", path_.string(),
                          errno);
            }
        }

        box_lock(const box_lock&) = delete;
        box_lock& operator=(const box_lock&) = delete;

        ~box_lock() {
            if (fd_ >= 0) {
                ::close(fd_);  // releases the flock.
            }
        }

        /// @brief Whether a prior `--init` marked this box initialized.
        bool is_initialized() const {
            lock_record rec = read_record();
            return rec.magic == LOCK_MAGIC && rec.initialized != 0;
        }

        /// @brief Persist a full record marking the box initialized (`--init`).
        void mark_initialized() {
            write_record(lock_record{.magic = LOCK_MAGIC, .initialized = 1});
        }

        /// @brief Clear the record by truncating to zero, never unlinking, so
        /// the box reads as uninitialized again (`--cleanup`). Idempotent.
        void clear() {
            if (::ftruncate(fd_, 0) < 0) {
                terminate("Cannot truncate box lock {}: errno {}",
                          path_.string(), errno);
            }
        }

       private:
        int fd_ = -1;
        fs::path path_;

        /// @brief Read the record from offset 0; a short read (empty file)
        /// yields a zeroed record, i.e. uninitialized.
        lock_record read_record() const {
            lock_record rec{};
            ssize_t n = ::pread(fd_, &rec, sizeof(rec), 0);
            if (n < 0) {
                terminate("Cannot read box lock {}: errno {}", path_.string(),
                          errno);
            }
            if (static_cast<std::size_t>(n) < sizeof(rec)) {
                return lock_record{};
            }
            return rec;
        }

        /// @brief Overwrite the record at offset 0.
        void write_record(const lock_record& rec) {
            ssize_t n = ::pwrite(fd_, &rec, sizeof(rec), 0);
            if (n != static_cast<ssize_t>(sizeof(rec))) {
                terminate("Cannot write box lock {}: errno {}", path_.string(),
                          errno);
            }
        }
    };
}  // namespace lock

#endif
