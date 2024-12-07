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

namespace cgroup 
{
    namespace fs = std::filesystem;

    inline auto const& ROOT_CG_PATH()
    {
        static fs::path path("/sys/fs/cgroup");
        return path;
    }

    inline auto const& CGROUP_CONTROLLERS()
    {
        static fs::path fname("cgroup.controllers");
        return fname;
    }

    inline auto const& CGROUP_SUBTREE_CONTROL()
    {
        static fs::path fname("cgroup.subtree_control");
        return fname;
    }

    inline auto const& CGROUP_PROCS()
    {
        static fs::path fname("cgroup.procs");
        return fname;
    }

    inline auto const& CPU_MAX()
    {
        static fs::path fname("cpu.max");
        return fname;
    }

    inline auto const& CPU_STAT()
    {
        static fs::path fname("cpu.stat");
        return fname;
    }

    inline auto const& MEMORY_MAX()
    {
        static fs::path fname("memory.max");
        return fname;
    }

    inline auto const& MEMORY_PEAK()
    {
        static fs::path fname("memory.peak");
        return fname;
    }

    inline auto const& MEMORY_MIN()
    {
        static fs::path fname("memory.min");
        return fname;
    }
    inline fs::path cg_abs_path(const fs::path& cg_rel_path)
    {
        return fs::path(ROOT_CG_PATH() / cg_rel_path);
    }

    inline size_t cpu_usage_usec_rel(const fs::path& cg_rel_path)
    {
        std::ifstream cpu_stat(ROOT_CG_PATH() / cg_rel_path / CPU_STAT());
        return std::stoi(file_utils::read_row_col(cpu_stat,0,0));
    }

    inline size_t cpu_usage_usec_abs(const fs::path& cg_path)
    {
        std::ifstream cpu_stat(cg_path / CPU_STAT());
        return std::stoi(file_utils::read_row_col(cpu_stat,0,0));
    }

    inline size_t memory_usage_bytes_rel(const fs::path& cg_rel_path)
    {
        std::ifstream memory_peak(ROOT_CG_PATH() / cg_rel_path / MEMORY_PEAK());
        return std::stoi(file_utils::read_row_col(memory_peak,0,0));
    }

    inline size_t memory_usage_bytes_abs(const fs::path& cg_path)
    {
        std::ifstream memory_peak(cg_path / MEMORY_PEAK());
        return std::stoi(file_utils::read_row_col(memory_peak,0,0));
    }

    /**
     * @brief Offers an interface for one resource controller for a specific cgroup.
     */
    class controller
    {
    protected:
        const fs::path* cgrp_path_;

        virtual const std::string& cntrlr_type() = 0;

    public:
        controller(const fs::path& path) : cgrp_path_(&path) {}

        bool enable()
        {
            //    "echo +type >> /sys/fs/cgroup/cgroup.subtree_control"

            fs::path subtree_control(*cgrp_path_ / CGROUP_SUBTREE_CONTROL());
            bool success = file_utils::append_text(subtree_control, "+" + cntrlr_type());
            return success;
        }
    private:
    };

    class cpu_cntrlr : public controller
    {
        inline static const std::string type = "cpu";
    public:
        using controller::controller;

        bool set_cpu_max(unsigned int percentage)
        {
            if(percentage > 100) return false;

            fs::path cpu_max(*cgrp_path_ / CPU_STAT());
            bool success = file_utils::write_formatted(cpu_max, "{} {}", percentage*1000, 100000);
            
            return success;
        }

    protected:
        const std::string& cntrlr_type() override
        {
            return type;
        }
    };

    class memory_cntrlr : public controller
    {   
        inline static const std::string type = "memory";
    public:
        using controller::controller;

        /*
        @note Rounds the limit down to the nearest power of two.
        */
        bool set_memory_max(unsigned int bytes)
        {
            //  echo "$BYTES" > memory.max

            fs::path memory_max(*cgrp_path_ / MEMORY_MAX());
            bool success = file_utils::write_formatted(memory_max, "{}", bytes);
            
            return success;
        }
        bool set_memory_min_to_max()
        {
            //  echo max > memory.min

            fs::path memory_min(*cgrp_path_ / MEMORY_MIN());
            bool success = file_utils::write_formatted(memory_min,"max");
            
            return success;
        }
    protected:
        const std::string& cntrlr_type() override
        {
            return type;
        }

    };
        

    class cgroupv2_t
    {
    public:
        cgroupv2_t(const fs::path& rel_cgrp_path) : cgrp_path_(ROOT_CG_PATH() / rel_cgrp_path),
                                                    cpu_(cgrp_path_),
                                                    mem_(cgrp_path_)
        {
            reset_path();
        }

        ~cgroupv2_t()
        {
            close_fd();
        }

        int open_fd()
        {
            fd_ = open(cgrp_path_.c_str(), O_DIRECTORY | O_RDONLY);
            return fd_.value();
        }

        void close_fd()
        {
            if(fd_.has_value())
            {
                if(close(fd_.value()))
                    logs::error("File descriptor for cgroup {} didnt close", cgrp_path_.c_str());
                fd_.reset();
            }
        }

        void enable_all_cntrlrs()
        {
            cpu_.enable();
            mem_.enable();
        }

        
        bool add_me()
        {
            auto mypid = getpid();
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            return file_utils::write_formatted(cgroup_procs, "{}", mypid);
        }

        size_t cpu_usage_usec()
        {
            return cpu_usage_usec_abs(cgrp_path_);
        }

        size_t memory_usage_bytes()
        {
            return memory_usage_bytes_abs(cgrp_path_);
        }

        void set_strict_memory_limit(size_t bytes)
        {
            mem_.set_memory_max(bytes);
            mem_.set_memory_min_to_max();
        }

        void list_procs()
        {
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            file_utils::print_lines(cgroup_procs);
        }
    private:
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


        const fs::path cgrp_path_;
        

        cpu_cntrlr cpu_;
        memory_cntrlr mem_;
        std::optional<int> fd_;
    };
    class root_cgroupv2_t
    {
    public:
        root_cgroupv2_t() :  cgrp_path_(ROOT_CG_PATH()),
                        cpu_(ROOT_CG_PATH()),
                        mem_(ROOT_CG_PATH())
        {}

        void enable_all_cntrlrs()
        {
            cpu_.enable();
            mem_.enable();
        }
        
        bool add_me()
        {
            auto mypid = getpid();
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            return file_utils::write_formatted(cgroup_procs, "{}", mypid);
        }

        size_t cpu_usage_usec()
        {
            return cpu_usage_usec_abs(cgrp_path_);
        }

        size_t memory_usage_bytes()
        {
            return memory_usage_bytes_abs(cgrp_path_);
        }

        void set_strict_memory_limit(size_t bytes)
        {
            mem_.set_memory_max(bytes);
            mem_.set_memory_min_to_max();
        }

        void list_procs()
        {
            auto cgroup_procs(cgrp_path_ / CGROUP_PROCS());
            file_utils::print_lines(cgroup_procs);
        }
    private:
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


        const fs::path cgrp_path_;
        

        cpu_cntrlr cpu_;
        memory_cntrlr mem_;
        std::optional<int> fd_;
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
        {
        }
        
        ~root_cgroup_manager()
        {
            cleanup();
        }

        void run()
        {
            root_cgrp_ = std::move(std::make_unique<cgroupv2_t>("container_instance"));
            leaf_cgrp_ = std::move(std::make_unique<cgroupv2_t>("container_instance/leaf"));
            proxy_cgrp_ = std::move(std::make_unique<cgroupv2_t>("container_instance/proxy"));
            leaf_cgrp_->add_me();
            root_cgrp_->enable_all_cntrlrs();
            //proxy_cgrp_->enable_all_cntrlrs(); 
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
