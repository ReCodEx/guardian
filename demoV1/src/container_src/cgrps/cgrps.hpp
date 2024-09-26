#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>
#include <format>
#include <unistd.h>
#include <fcntl.h>

#include "utils.hpp"

namespace cgroup 
{
    namespace fs = std::filesystem;
    constexpr std::string_view CGRP_FS_PATH_ = "/sys/fs/cgroup";
    constexpr std::string_view CGROUP_CONTROLLERS_ = "/cgroup.controllers";
    constexpr std::string_view CGROUP_SUBTREE_CONTROL_ = "/cgroup.subtree_control";
    constexpr std::string_view CPU_MAX_ = "/cpu.max";

    inline auto const& ROOT_CG_PATH()
    {
        static fs::path path("/sys/fs/cgroup/rcdx");
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


    struct cgrp_config
    {

    };

    class root_cgroup_manager
    {
    public:
        root_cgroup_manager() {}
        root_cgroup_manager(const cgrp_config& config){}
    };



    /**
     * @brief Offers an interface for one resource controller for a specific cgroup.
     */
    class cntrlr_operator
    {
    protected:
        const fs::path* cgrp_path_;

        virtual const std::string& cntrlr_type() = 0;

    public:
        cntrlr_operator(const fs::path& path) : cgrp_path_(&path) {}

        bool enable_cntrlr()
        {
            //    "echo +type >> /sys/fs/cgroup/cgroup.subtree_control"

            fs::path subtree_control(*cgrp_path_ / CGROUP_SUBTREE_CONTROL());
            std::string text("+" + cntrlr_type());
            bool success = file_utils::append_text(subtree_control, "+" + cntrlr_type());
            return success;
        }
    private:
    };

    class cpu_cntrlr : public cntrlr_operator
    {
        inline static const std::string type = "cpu";
    public:
        using cntrlr_operator::cntrlr_operator;

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

    class memory_cntrlr : public cntrlr_operator
    {
        inline static const std::string type = "memory";
    public:
        using cntrlr_operator::cntrlr_operator;

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
    protected:
        const std::string& cntrlr_type() override
        {
            return type;
        }

    };
        

    class cgroupv2_t
    {
    public:
        cgroupv2_t() :  cgrp_path_(ROOT_CG_PATH()),
                        cpu_(ROOT_CG_PATH()),
                        mem_(ROOT_CG_PATH())
        {
            init_path();
            enable_all_cntrlrs();
        }

        cgroupv2_t(const fs::path& rel_cgrp_path) : cgrp_path_(ROOT_CG_PATH() / rel_cgrp_path),
                                                    cpu_(cgrp_path_),
                                                    mem_(cgrp_path_)
        {
            reset_path();
            //enable_all_cntrlrs();
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
                close(fd_.value());
                fd_.reset();
            }
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

        void enable_all_cntrlrs()
        {
            cpu_.enable_cntrlr();
            mem_.enable_cntrlr();
        }


        const fs::path cgrp_path_;
        

        cpu_cntrlr cpu_;
        memory_cntrlr mem_;
        std::optional<int> fd_;
    };

}

#endif
