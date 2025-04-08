#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>
#include <format>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>

#include "utils.hpp"
#include "terminate.hpp"
#include "credentials.hpp"

namespace cgroup 
{
    namespace fs = std::filesystem;

    /// @brief The default path to the cgroup virtual filesystem.
    inline auto const& CGROUP_FS_PATH()
    {
        static fs::path path("/sys/fs/cgroup");
        return path;
    }

    /**
     * @brief Base class for cgroup resource controller classes.
     */
    class controller
    {
    protected:
        /// @brief Pointer to the path of the cgroup (member of cgroupv2_t).
        const fs::path* cgrp_path_;

        /// @brief String with the type of the controller (written to cgroup.subtree_control to enable in child cgroups)
        virtual const std::string& cntrlr_type() const = 0;

    public:
        controller()
        {}
        
        /// @brief 
        /// @param path reference to the cgroup path (stored as a member in cgroupv2_t) 
        controller(const fs::path& path) : cgrp_path_(&path) {}

        /// @brief Enable this controller for child cgroups.
        /// @return 
        bool enable()
        {
            //    "echo +type >> /sys/fs/cgroup/cgroup.subtree_control"

            fs::path subtree_control(*cgrp_path_ / CGROUP_SUBTREE_CONTROL());
            bool success = file_utils::append_text(subtree_control, "+" + cntrlr_type());
            return success;
        }
    private:
        /// @brief Name of the cgroup.subtree_control file.
        /// @return 
        static const fs::path& CGROUP_SUBTREE_CONTROL()
        {
            static fs::path fname("cgroup.subtree_control");
            return fname;
        }
    };

    /// @brief Interface for the cgroup cpu controller.
    class cpu_cntrlr : public controller
    {
        inline static const std::string type = "cpu";
    public:
        using controller::controller;

        /// @brief Extractor for the total cpu usage of a cgroup from the cpu.stat file.
        /// @return cpu time in microseconds
        size_t cpu_usage_usec() const
        {
            std::ifstream cpu_stat(*cgrp_path_ / CPU_STAT());
            return std::stoi(file_utils::read_row_col(cpu_stat,0,0));
        }
    protected:
        /// @brief Override the cntrlr_type() with "cpu".
        /// @return "cpu"
        const std::string& cntrlr_type() const override
        {
            return type;
        }

    private:
        /// @brief Name of the cgroup cpu.max file.
        static const fs::path& CPU_MAX()
        {
            static fs::path fname("cpu.max");
            return fname;
        }

        /// @brief Name of the cgroup cpu.stat file.
        static const fs::path& CPU_STAT()
        {
            static fs::path fname("cpu.stat");
            return fname;
        }
    };

    /// @brief Interface for the cgroup memory controller.
    class memory_cntrlr : public controller
    {   
        inline static const std::string type = "memory";
    public:
        using controller::controller;

        /// @brief Set the value in the memory.max file to 'bytes'
        /// @param bytes 
        /// @return true if the write succeeded.
        bool set_memory_max(unsigned int bytes)
        {
            //  echo "$BYTES" > memory.max

            fs::path memory_max(*cgrp_path_ / MEMORY_MAX());
            bool success = file_utils::write_formatted(memory_max, "{}", bytes);
            
            return success;
        }

        ///
        /// @brief Set the value in the memory.max file to "min"
        /// @return true if the write succeeded.
        bool set_memory_min_to_max()
        {
            //  echo max > memory.min

            fs::path memory_min(*cgrp_path_ / MEMORY_MIN());
            bool success = file_utils::write_formatted(memory_min,"max");
            
            return success;
        }

        /// @brief Extractor for the memory usage of a cgroup from the memory.peak file.
        /// @return memory usage in bytes
        size_t memory_usage_bytes() const
        {
            std::ifstream memory_peak(*cgrp_path_ / MEMORY_PEAK());
            return std::stoi(file_utils::read_row_col(memory_peak,0,0));
        }
    protected:
        const std::string& cntrlr_type() const override
        {
            return type;
        }
    private:
        /// @brief Name of the cgroup memory.max file.
        /// @return 
        static const fs::path& MEMORY_MAX()
        {
            static fs::path fname("memory.max");
            return fname;
        }

        /// @brief Name of the cgroup memory.peak file.
        /// @return 
        static const fs::path& MEMORY_PEAK()
        {
            static fs::path fname("memory.peak");
            return fname;
        }
        
        /// @brief Name of the cgroup memory.min file.
        /// @return 
        static const fs::path& MEMORY_MIN()
        {
            static fs::path fname("memory.min");
            return fname;
        }
    };

    /// @brief Interface for the cgroup pids controller.
    class pid_cntrlr : public controller
    {   
        inline static const std::string type = "pids";
    public:
        using controller::controller;

        /// @brief Set the value in the pids.max file to 'count'.
        /// @param count
        /// @return true if the write was a success.
        bool set_pids_max(size_t count)
        {
            //  echo "$count" > pids.max

            fs::path pids_max(*cgrp_path_ / PIDS_MAX());
            bool success = file_utils::write_formatted(pids_max, "{}", count);
            return success;
        }
    protected:
        const std::string& cntrlr_type() const override
        {
            return type;
        }
    private:
        /// @brief Name of the cgroup pids.max file.
        static const fs::path& PIDS_MAX()
        {
            static fs::path fname("pids.max");
            return fname;
        }
    };

    /// @brief Interface for using a cgroup and its controllers.
    class cgroupv2_t
    {
    public:
        cgroupv2_t()
        {}
        
        /// @brief 
        /// @param rel_cgrp_path Relative path of the cgroup (excluding the path to the cgroup filesystem).
        cgroupv2_t(const fs::path& rel_cgrp_path) : cgrp_path_(CGROUP_FS_PATH() / rel_cgrp_path),
                                                    cpu_(cgrp_path_),
                                                    mem_(cgrp_path_),
                                                    pid_(cgrp_path_)
        {
            init_path();
        }

        ~cgroupv2_t()
        {
            close_fd();
        }

        /// @brief Open a file descriptor pointing to the cgroup directory (used in clone3() with the CLONE_INTO_CGROUP flag).
        int open_fd()
        {
            fd_ = open(cgrp_path_.c_str(), O_DIRECTORY | O_RDONLY);
            return fd_.value();
        }

        /// @brief Close the file descriptor opened for this cgroup.
        void close_fd()
        {
            if(fd_.has_value())
            {
                if(close(fd_.value()))
                    logs::error("File descriptor for cgroup {} didnt close", cgrp_path_.c_str());
                fd_.reset();
            }
        }

        /// @brief Enable all relevant controllers (cpu, memory, pids) for child cgroups.
        void enable_all_cntrlrs()
        {
            if(!(cpu_.enable() && mem_.enable() && pid_.enable()))
                { terminate("Failed to enable cgroup controllers in {}", cgrp_path_.string()); }
        }

        /// @brief Add the current process to this cgroup ( the current PID to the cgroup.procs file).
        /// @return True if the write succeeded.
        bool add_me()
        {
            auto mypid = getpid();
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            return file_utils::write_formatted(cgroup_procs, "{}", mypid);
        }

        /// @brief Getter for the cpu time used by this cgroup.
        /// @return Cpu time in microseconds.
        size_t cpu_usage_usec() const
        {
            return cpu_.cpu_usage_usec();
        }

        /// @brief Getter for the amount of memory used by this cgroup.
        /// @return Memory usage in bytes.
        size_t memory_usage_bytes() const
        {
            return mem_.memory_usage_bytes();
        }

        /// @brief Setup the memory controller so that processes are killed upon exceeding the memory limit.
        /// @param bytes 
        /// @note Swap has to disabled in order for this to work properly.
        void set_strict_memory_limit(size_t bytes)
        {
            mem_.set_memory_max(bytes);
            mem_.set_memory_min_to_max();
        }

        /// @brief Set the limit of PIDS in this cgroup to 'n'.
        /// @param n 
        void set_processes_limit(size_t n)
        {
            pid_.set_pids_max(n);
        }

        /// @brief Print the PIDS in this cgroup to stdout (for debugging).
        void list_procs() const
        {
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            file_utils::print_lines(cgroup_procs);
        }

    private:
        /// @brief Absolute path of the cgroup (including path to the cgroup filesystem).
        const fs::path cgrp_path_;

        /// @brief Cpu controller
        cpu_cntrlr cpu_;
        
        /// @brief Memory controller
        memory_cntrlr mem_;
        
        /// @brief PIDS controller
        pid_cntrlr pid_;
        
        /// @brief Optional file descriptor used in clone3() with CLONE_INTO_CGROUP.
        std::optional<int> fd_;

        /// @brief Name of the cgroup.procs file.
        /// @return 
        static const fs::path& CGROUP_PROCS()
        {
            static fs::path fname("cgroup.procs");
            return fname;
        }

        /// @brief Create the cgroup.
        void init_path()
        {
            if(!fs::is_directory(cgrp_path_))
            {
                if(!fs::create_directory(cgrp_path_))
                {
                    terminate("Creating the cgroup {} failed", cgrp_path_.string());
                }
            }
        }

        /// @brief Delete the cgroup with this path and create a new one.
        void reset_path()
        {
            if(fs::is_directory(cgrp_path_))
            {
                fs::remove(cgrp_path_);
            }
            if(!fs::create_directory(cgrp_path_))
            {
                terminate("Creating the cgroup {} failed", cgrp_path_.string());
            }
        }

    };

    /// @brief Interface for the root cgroup (/sys/fs/cgroup).
    class root_cgroupv2_t
    {
    public:
        root_cgroupv2_t() :  cgrp_path_(CGROUP_FS_PATH()),
                        cpu_(CGROUP_FS_PATH()),
                        mem_(CGROUP_FS_PATH()),
                        pid_(CGROUP_FS_PATH())
        {}

        /// @brief Enable relevant controllers (cpu, memory, pids) for child cgroups.
        void enable_all_cntrlrs()
        {
            if(!(cpu_.enable() && mem_.enable() && pid_.enable()))
                { terminate("Failed to enable cgroup controllers in the root cgroup"); }
        }
        
    private:
        /// @brief Absolute path of the cgroup (including path to the cgroup filesystem).
        const fs::path cgrp_path_;

        /// @brief Cpu controller
        cpu_cntrlr cpu_;
        
        /// @brief Memory controller
        memory_cntrlr mem_;
        
        /// @brief PIDS controller
        pid_cntrlr pid_;
    };

    /// @brief Manager class for the proxy level of the cgroup hierarchy.
    class proxy_cgroup_manager
    {
    public:
        /// @brief Prepare the proxy cgroup hierarchy. Has to be run AFTER proxy_mount_manager::run().
        /// @details The task_supervisor class is responsible for the task cgroup, so this effectively only enables controllers.
        void run()
        {
            leaf_cgrp_ = std::make_unique<cgroupv2_t>("proxy_leaf");
            if(!leaf_cgrp_->add_me())
                { terminate("Failed to move the proxy process to its cgroup"); }
            root_cgrp_.enable_all_cntrlrs();
        }
    private:
        /// @brief Interface for the root cgroup (/sys/fs/cgroup).
        root_cgroupv2_t root_cgrp_;
        
        /// @brief The proxy process is placed here because we cant enable controllers in a populated cgroup.
        std::unique_ptr<cgroupv2_t> leaf_cgrp_;
    };

    /// @brief Manager class for the root level of the cgroup hierarchy.
    class root_cgroup_manager
    {
    public:
        root_cgroup_manager()
        {}
        
        /// @brief 
        /// @param config
        /// @param credentials
        root_cgroup_manager(const config::root_configuration& config, credentials::root_credentials_manager& credentials) : 
        config_(&config), 
        credentials_(&credentials)
        {}

        ~root_cgroup_manager()
        {
            cleanup();
        }

        /// @brief Setup the box and proxy cgroup.
        void run()
        {
            fs::path box_cg = config_->get_box_cgroup(fs::path(std::to_string(credentials_->box_id()))) ;
            box_cgrp_   = std::make_unique<cgroupv2_t>(box_cg);
            leaf_cgrp_  = std::make_unique<cgroupv2_t>(box_cg / fs::path("leaf"));
            proxy_cgrp_ = std::make_unique<cgroupv2_t>(box_cg / fs::path("proxy"));

            root_cgrp_.enable_all_cntrlrs();
            leaf_cgrp_->add_me();
            box_cgrp_->enable_all_cntrlrs();
        }

        /// @brief Open a file descriptor pointing to the proxy cgroup directory (used in clone3() with the CLONE_INTO_CGROUP flag).
        /// @return open() return value.
        int open_proxy_fd()
        {
            return proxy_cgrp_->open_fd();
        }

        /// @brief Close the fd pointing to the proxy cgroup directory (won't fail if it isn't open).
        void close_proxy_fd()
        {
            proxy_cgrp_->close_fd();
        }
    private:
        /// @brief 
        const config::root_configuration* config_;
        
        /// @brief 
        const credentials::root_credentials_manager* credentials_;
        
        /// @brief Interface for the root cgroup (/sys/fs/cgroup).
        root_cgroupv2_t root_cgrp_;
        
        /// @brief The root cgroup of this box.
        std::unique_ptr<cgroup::cgroupv2_t> box_cgrp_;
        
        /// @brief The cgroup for the proxy and the root of the cgroup namespace that the proxy runs in.
        std::unique_ptr<cgroup::cgroupv2_t> proxy_cgrp_;
        
        /// @brief The root process is placed here because we cant enable controllers in a populated cgroup.
        std::unique_ptr<cgroup::cgroupv2_t> leaf_cgrp_;

        void cleanup()
        {
            proxy_cgrp_->close_fd();
        }
    };
}
#endif
