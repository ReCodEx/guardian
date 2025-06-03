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

    /// @brief Default path to the cgroup virtual filesystem.
    inline auto const& CGROUP_FS_PATH()
    {
        static fs::path path("/sys/fs/cgroup");
        return path;
    }

    
    /// @brief Base class for cgroup resource controller classes.
    class controller
    {
    protected:
        /// @brief Pointer to the path of the cgroup ( stored in the cgroupv2_t class this controller belongs to).
        const fs::path* cgrp_path_;

        /// @brief String with the type of the controller (written to cgroup.subtree_control to enable this controller in child cgroups)
        virtual const std::string& cntrlr_type() const = 0;

    public:
        controller()
        {}
        
        /// @brief Constructor.
        /// @param path reference to the path of the cgroup ( stored in the cgroupv2_t class this controller belongs to ). 
        controller(const fs::path& path) : cgrp_path_(&path) {}

        /// @brief Enable this controller for child cgroups by writing into the cgroup.subtree_control file.
        /// @return True if the write succeeded.
        bool enable()
        {
            // Equivalent to "echo +type >> cgrp_path_/cgroup.subtree_control".

            fs::path subtree_control(*cgrp_path_ / CGROUP_SUBTREE_CONTROL());
            bool success = file_utils::append_text(subtree_control, "+" + cntrlr_type());
            return success;
        }
    private:
        /// @brief Returns the name of the cgroup.subtree_control file.
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
        /// @brief Override cntrlr_type() with "cpu".
        /// @return "cpu"
        const std::string& cntrlr_type() const override
        {
            return type;
        }

    private:
        /// @brief Returns the name of the cgroup cpu.max file.
        static const fs::path& CPU_MAX()
        {
            static fs::path fname("cpu.max");
            return fname;
        }

        /// @brief Returns the name of the cgroup cpu.stat file.
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
            //  Equivalent to "echo $BYTES > memory.max"

            fs::path memory_max(*cgrp_path_ / MEMORY_MAX());
            if(!file_utils::write_formatted(memory_max, "{}", bytes))
                { terminate("Failed to write memory.max in cgroup {}", cgrp_path_->string()); }
            return true;
        }

        ///
        /// @brief Set the value in the memory.max file to "min"
        /// @return true if the write succeeded.
        bool set_memory_min_to_max()
        {
            // Equivalent to "echo max > memory.min".

            fs::path memory_min(*cgrp_path_ / MEMORY_MIN());
            if(!file_utils::write_formatted(memory_min,"max"))
                { terminate("Failed to write memory.min in cgroup {}", cgrp_path_->string()); }
            
            return true;
        }

        /// @brief Extractor for the memory usage of a cgroup from the memory.peak file.
        /// @return memory usage in bytes
        size_t memory_usage_bytes() const
        {
            std::ifstream memory_peak(*cgrp_path_ / MEMORY_PEAK());
            return std::stoi(file_utils::read_row_col(memory_peak,0,0));
        }
    protected:
        /// @brief Override cntrlr_type() with "memory".
        /// @return "memory"
        const std::string& cntrlr_type() const override
        {
            return type;
        }
    private:
        /// @brief Returns the name of the cgroup memory.max file.
        static const fs::path& MEMORY_MAX()
        {
            static fs::path fname("memory.max");
            return fname;
        }

        /// @brief Returns the name of the cgroup memory.peak file.
        static const fs::path& MEMORY_PEAK()
        {
            static fs::path fname("memory.peak");
            return fname;
        }
        
        /// @brief Returns the name of the cgroup memory.min file.
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
            // Equivalent to "echo $count > pids.max"

            fs::path pids_max(*cgrp_path_ / PIDS_MAX());
            bool success = file_utils::write_formatted(pids_max, "{}", count);
            return success;
        }
    protected:
        /// @brief Override cntrlr_type() with "pids".
        /// @return "pids"
        const std::string& cntrlr_type() const override
        {
            return type;
        }
    private:
        /// @brief Returns the name of the cgroup pids.max file.
        static const fs::path& PIDS_MAX()
        {
            static fs::path fname("pids.max");
            return fname;
        }
    };

    /// @brief Supervisor class for using a cgroup and its controllers.
    class cgroupv2_t
    {
    public:
        cgroupv2_t()
        {}
        
        /// @brief Constructor. 
        /// @param rel_cgrp_path Relative path of the cgroup (excluding the path to the cgroup filesystem).
        cgroupv2_t(const fs::path& rel_cgrp_path) : cgrp_path_(CGROUP_FS_PATH() / rel_cgrp_path),
                                                    cpu_(cgrp_path_),
                                                    mem_(cgrp_path_),
                                                    pid_(cgrp_path_)
        {
            init();
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

        /// @brief Close the file descriptor opened for this cgroup (Does nothing if open_fd() hasn't been called ).
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

        /// @brief Add the current process to this cgroup (the current PID to the cgroup.procs file).
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

        /// @brief Setup the memory controller so that processes are killed upon exceeding a limit on memory utilization.
        /// @param bytes The limit in bytes.
        /// @note Swap has to disabled in order for this to work properly.
        /// @details As I understand from experimenting, the memory.min is a soft limit which causes lighter page reclaim when exceeded.
        /// memory.max causes very aggresive page reclaim when exceeded and killing the process if pages can't be reclaimed. So we set the memory.min
        /// to "max", not to get in the way and the memory.max to the intended limit. But swap has to be disabled even with this setup.
        void set_strict_memory_limit(size_t bytes)
        {
            logs::debug("Setting memory limit for cgroup {} to {} bytes", cgrp_path_.string(), bytes);
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
        void init()
        {
            if(fs::is_directory(cgrp_path_))
                { terminate("Cgroup '{}' already exists.", cgrp_path_.string()); }

            if(!fs::create_directory(cgrp_path_))
                { terminate("Creating cgroup '{}' failed", cgrp_path_.string()); }
        }
    };

    /// @brief Supervisor class for setting up the root cgroup (/sys/fs/cgroup).
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
        /// @brief Prepare the proxy level of the cgroup hierarchy for tasks to execute.
        /// Has to be called AFTER proxy_mount_manager::run().
        ///
        /// @note The cgroup the proxy initially runs in is created BEFORE cloning the proxy with CLONE_INTO_CGROUP.
        /// Together with CLONE_NEWCGROUP, this is a convenient way to both launch the process in a specific cgroup and set this cgroup as the root 
        /// of the new namespace.
        ///
        /// @details The main responsibility of this method is enabling controllers to be used deeper in the hierarchy. 
        /// Before doing that, a "proxy_leaf" cgroup is created, where the proxy is placed. This is because enabling controllers isn't possible in a
        /// cgroup populated by a process.
        void run()
        {
            leaf_cgrp_ = std::make_unique<cgroupv2_t>("proxy_leaf");
            if(!leaf_cgrp_->add_me())
                { terminate("Failed to move the proxy process to its cgroup"); }
            root_cgrp_.enable_all_cntrlrs();
        }
    private:
        /// @brief Interface for the cgroup the proxy is launched in, which is now the root cgroup in the new namespace. (/sys/fs/cgroup).
        root_cgroupv2_t root_cgrp_;
        
        /// @brief Leaf cgroup for the proxy process. See the description of run().
        std::unique_ptr<cgroupv2_t> leaf_cgrp_;
    };

    /// @brief Manager class for the root level of the cgroup hierarchy.
    class root_cgroup_manager
    {
    public:
        root_cgroup_manager()
        {}
        
        /// @brief Constructor.
        /// @param config Root node of configuration.
        /// @param credentials Reference to credentials_manager, which is called to obtain root cgroup of this instance.
        root_cgroup_manager(const config::root_configuration& config, credentials::root_credentials_manager& credentials) : 
        config_(&config), 
        credentials_(&credentials)
        {}

        ~root_cgroup_manager()
        {
            cleanup();
        }

        /// @brief Setup the root and proxy cgroups.
        /// @details This method creates the root and proxy cgroups for this instance, and enables controllers for the proxy cgroup.
        /// Before enabling controllers, a leaf cgroup is created, where the root process is placed. This is needed because enabling controllers isn't possible in a
        /// cgroup populated by a process.
        void run()
        {
            fs::path instance_cg = credentials_->instance_cgroup();
            instance_cgrp_   = std::make_unique<cgroupv2_t>(instance_cg);
            leaf_cgrp_  = std::make_unique<cgroupv2_t>(instance_cg / fs::path("leaf"));
            proxy_cgrp_ = std::make_unique<cgroupv2_t>(instance_cg / fs::path("proxy"));

            root_cgrp_.enable_all_cntrlrs();
            leaf_cgrp_->add_me();
            instance_cgrp_->enable_all_cntrlrs();
        }

        /// @brief Open a file descriptor pointing to the proxy cgroup directory (used in clone3() with the CLONE_INTO_CGROUP flag).
        /// @return open() return value.
        int open_proxy_fd()
        {
            return proxy_cgrp_->open_fd();
        }

        /// @brief Close the fd pointing to the proxy cgroup directory (won't fail if open_proxy_fd() hasn't been called).
        void close_proxy_fd()
        {
            proxy_cgrp_->close_fd();
        }
    private:
        /// @brief Root node of configuration.
        const config::root_configuration* config_;
        
        /// @brief Pointer to credentials_manager, which is called to obtain root cgroup of this instance.
        const credentials::root_credentials_manager* credentials_;
        
        /// @brief Interface for setting up the root of the cgroup filesystem. (/sys/fs/cgroup).
        root_cgroupv2_t root_cgrp_;
        
        /// @brief Root cgroup of this instance.
        std::unique_ptr<cgroup::cgroupv2_t> instance_cgrp_;
        
        /// @brief Proxy cgroup of this instance (and root of the sandbox cgroup namespace).
        std::unique_ptr<cgroup::cgroupv2_t> proxy_cgrp_;
        
        /// @brief Leaf cgroup for the root process. See the description of run().
        std::unique_ptr<cgroup::cgroupv2_t> leaf_cgrp_;

        void cleanup()
        {
            proxy_cgrp_->close_fd();
        }
    };
}
#endif
