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
    inline auto const& ROOT_CG_PATH()
    {
        static fs::path path("/sys/fs/cgroup");
        return path;
    }

    /// @brief Name of the cgroup.controllers file.
    /// @return 
    inline auto const& CGROUP_CONTROLLERS()
    {
        static fs::path fname("cgroup.controllers");
        return fname;
    }

    /// @brief Name of the cgroup.subtree_control file.
    /// @return 
    inline auto const& CGROUP_SUBTREE_CONTROL()
    {
        static fs::path fname("cgroup.subtree_control");
        return fname;
    }

    /// @brief Name of the cgroup.procs file.
    /// @return 
    inline auto const& CGROUP_PROCS()
    {
        static fs::path fname("cgroup.procs");
        return fname;
    }

    /// @brief Name of the cgroup cpu.max file.
    /// @return 
    inline auto const& CPU_MAX()
    {
        static fs::path fname("cpu.max");
        return fname;
    }

    /// @brief Name of the cgroup cpu.stat file.
    /// @return 
    inline auto const& CPU_STAT()
    {
        static fs::path fname("cpu.stat");
        return fname;
    }

    /// @brief Name of the cgroup memory.max file.
    /// @return 
    inline auto const& MEMORY_MAX()
    {
        static fs::path fname("memory.max");
        return fname;
    }

    /// @brief Name of the cgroup memory.peak file.
    /// @return 
    inline auto const& MEMORY_PEAK()
    {
        static fs::path fname("memory.peak");
        return fname;
    }
    
    /// @brief Name of the cgroup memory.min file.
    /// @return 
    inline auto const& MEMORY_MIN()
    {
        static fs::path fname("memory.min");
        return fname;
    }

    /// @brief Name of the cgroup pids.max file.
    /// @return 
    inline auto const& PIDS_MAX()
    {
        static fs::path fname("pids.max");
        return fname;
    }

    /// @brief Prepends the path to the cgroup filesystem to a relative cgroup path.
    /// @param cg_rel_path
    /// @return 
    inline fs::path cg_abs_path(const fs::path& cg_rel_path)
    {
        return fs::path(ROOT_CG_PATH() / cg_rel_path);
    }

    /// @brief Extractor for the cpu time of a cgroup from the cpu.stat file.
    /// @param cg_rel_path Relative path of the cgroup.
    /// @return cpu time in microseconds
    inline size_t cpu_usage_usec_rel(const fs::path& cg_rel_path)
    {
        std::ifstream cpu_stat(ROOT_CG_PATH() / cg_rel_path / CPU_STAT());
        return std::stoi(file_utils::read_row_col(cpu_stat,0,0));
    }

    /// @brief Extractor for the cpu time of a cgroup from the cpu.stat file.
    /// @param cg_path Absolute path of the cgroup (including path to the cgroup filesystem).
    /// @return cpu time in microseconds
    inline size_t cpu_usage_usec_abs(const fs::path& cg_path)
    {
        std::ifstream cpu_stat(cg_path / CPU_STAT());
        return std::stoi(file_utils::read_row_col(cpu_stat,0,0));
    }

    /// @brief Extractor for the memory usage of a cgroup from the memory.peak file.
    /// @param cg_rel_path Relative path of the cgroup (exluding path to the cgroup filesystem). 
    /// @return memory usage in bytes
    inline size_t memory_usage_bytes_rel(const fs::path& cg_rel_path)
    {
        std::ifstream memory_peak(ROOT_CG_PATH() / cg_rel_path / MEMORY_PEAK());
        return std::stoi(file_utils::read_row_col(memory_peak,0,0));
    }

    /// @brief Extractor for the memory usage of a cgroup from the memory.peak file.
    /// @param cg_path Absolute path of the cgroup (including path to the cgroup filesystem). 
    /// @return memory usage in bytes
    inline size_t memory_usage_bytes_abs(const fs::path& cg_path)
    {
        std::ifstream memory_peak(cg_path / MEMORY_PEAK());
        return std::stoi(file_utils::read_row_col(memory_peak,0,0));
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
    };

    /// @brief Interface for the cgroup cpu controller.
    class cpu_cntrlr : public controller
    {
        inline static const std::string type = "cpu";
    public:
        using controller::controller;

    protected:
        const std::string& cntrlr_type() const override
        {
            return type;
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
    protected:
        const std::string& cntrlr_type() const override
        {
            return type;
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
    };

    /// @brief Interface for using a cgroup and its controllers.
    class cgroupv2_t
    {
    public:
        /// @brief 
        /// @param rel_cgrp_path Relative path of the cgroup (excluding the path to the cgroup filesystem).
        cgroupv2_t(const fs::path& rel_cgrp_path) : cgrp_path_(ROOT_CG_PATH() / rel_cgrp_path),
                                                    cpu_(cgrp_path_),
                                                    mem_(cgrp_path_),
                                                    pid_(cgrp_path_)
        {
            /// TODO: see if I can remove the resetting of the path, looks like I can. 
            // reset_path();
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
                { terminate("Failed to enable cgroup controllers"); }
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
            return cpu_usage_usec_abs(cgrp_path_);
        }

        /// @brief Getter for the amount of memory used by this cgroup.
        /// @return Memory usage in bytes.
        size_t memory_usage_bytes() const
        {
            return memory_usage_bytes_abs(cgrp_path_);
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

    class root_cgroupv2_t
    {
    public:
        root_cgroupv2_t() :  cgrp_path_(ROOT_CG_PATH()),
                        cpu_(ROOT_CG_PATH()),
                        mem_(ROOT_CG_PATH()),
                        pid_(ROOT_CG_PATH())
        {}

        bool enable_all_cntrlrs()
        {
            return cpu_.enable() && mem_.enable() && pid_.enable();
        }
        
        bool add_me()
        {
            auto mypid = getpid();
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            return file_utils::write_formatted(cgroup_procs, "{}", mypid);
        }

        size_t cpu_usage_usec() const
        {
            return cpu_usage_usec_abs(cgrp_path_);
        }

        size_t memory_usage_bytes() const
        {
            return memory_usage_bytes_abs(cgrp_path_);
        }

        bool set_strict_memory_limit(size_t bytes)
        {
            return mem_.set_memory_max(bytes) && mem_.set_memory_min_to_max();
        }

        void list_procs() const
        {
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            file_utils::print_lines(cgroup_procs);
        }
    private:
        const fs::path cgrp_path_;

        cpu_cntrlr cpu_;
        memory_cntrlr mem_;
        pid_cntrlr pid_;
        std::optional<int> fd_;

        void init_path()
        {
            if(!fs::is_directory(cgrp_path_))
            {
                if(!fs::create_directory(cgrp_path_))
                {
                    throw std::runtime_error("Creating the cgroup failed");
                }
            }
        }

        void reset_path()
        {
            if(fs::is_directory(cgrp_path_))
            {
                fs::remove(cgrp_path_);
            }
            if(!fs::create_directory(cgrp_path_))
            {
                throw std::runtime_error("Creating the cgroup failed");
            }
        }
    };

    class proxy_cgroup_manager
    {
    public:
        void run()
        {
            leaf_cgrp_ = std::make_unique<cgroupv2_t>("proxy_leaf");
            leaf_cgrp_->add_me();
            root_cgrp_ = std::make_unique<root_cgroupv2_t>();
            root_cgrp_->enable_all_cntrlrs();
        }
    private:
        std::unique_ptr<root_cgroupv2_t> root_cgrp_;
        std::unique_ptr<cgroupv2_t> leaf_cgrp_;
    };

    class root_cgroup_manager
    {
    public:
        root_cgroup_manager()
        {}
        
        root_cgroup_manager(const config::root_interface& config, credentials::root_credentials_manager& credentials) : 
        config_(&config), 
        credentials_(&credentials)
        {}

        ~root_cgroup_manager()
        {
            cleanup();
        }

        void run()
        {
            fs::path root_cg = config_->get_box_cgroup(fs::path(std::to_string(credentials_->box_id()))) ;
            root_cgrp_ = std::move(std::make_unique<cgroupv2_t>(root_cg));
            leaf_cgrp_ = std::move(std::make_unique<cgroupv2_t>(root_cg / fs::path("leaf")));
            proxy_cgrp_ = std::move(std::make_unique<cgroupv2_t>(root_cg / fs::path("proxy")));
            leaf_cgrp_->add_me();
            root_cgrp_->enable_all_cntrlrs();
        }

        int open_proxy_fd()
        {
            return proxy_cgrp_->open_fd();
        }

        void close_proxy_fd()
        {
            proxy_cgrp_->close_fd();
        }
    private:
        const config::root_interface* config_;
        const credentials::root_credentials_manager* credentials_;
        std::unique_ptr<cgroup::cgroupv2_t> root_cgrp_;
        std::unique_ptr<cgroup::cgroupv2_t> proxy_cgrp_;
        std::unique_ptr<cgroup::cgroupv2_t> leaf_cgrp_;

        void setup_proxy_cgroup()
        {
            //proxy_cgrp_.enable_all_cntrlrs();
        }
        
        void cleanup()
        {
            proxy_cgrp_->close_fd();
        }
    };
}
#endif
