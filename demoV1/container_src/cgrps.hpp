#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>
#include <format>

#include "utils.hpp"

namespace cgrp_management 
{
    namespace fs = std::filesystem;
    constexpr std::string_view CGRP_FS_PATH_ = "/sys/fs/cgroup";
    constexpr std::string_view CGROUP_CONTROLLERS_ = "/cgroup.controllers";
    constexpr std::string_view CGROUP_SUBTREE_CONTROL_ = "/cgroup.subtree_control";
    constexpr std::string_view CPU_MAX_ = "/cpu.max";

    inline auto const& CGRP_FS_PATH()
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

    inline auto const& CPU_MAX()
    {
        static fs::path fname("cpu.max");
        return fname;
    }

    inline auto const& MEMORY_MAX()
    {
        static fs::path fname("memory.max");
        return fname;
    }


    class cgroupv2_t
    {
        const fs::path _cgrp_path;
    public:
        cgroupv2_t(const fs::path& rel_cgrp_path) : _cgrp_path(CGRP_FS_PATH() / rel_cgrp_path)
        {
            bool created = fs::create_directory(_cgrp_path);

            if(!created)
            {
                throw std::runtime_error("Creating the cgroup failed");
            }
        }

        ~cgroupv2_t()
        {
            bool removed = fs::remove(_cgrp_path);

            if(!removed)
            {
                std::cout << "Weird, cgroup removal failed!";
            }
        }

        void view_cpu_max()
        {
            std::ifstream cntrls(_cgrp_path / CPU_MAX());
            if(cntrls.is_open())
            {
                std::cout << cntrls.rdbuf() << std::endl;
            }
        }

        std::string get_cgrp_path() const
        {
            return _cgrp_path.string();
        }
    };


    /**
     * @brief Offers an interface for one resource controller for a specific cgroup.
     */
    class cntrlr_operator
    {
    protected:
        const std::string& _cgrp_path;

        virtual const std::string& cntrlr_type() = 0;

    public:
        cntrlr_operator(const std::string& path) : _cgrp_path(path) {}

        bool enable_cntrlr_root()
        {
            //    "echo +type >> /sys/fs/cgroup/cgroup.subtree_control"

            fs::path subtree_control = _cgrp_path / CGROUP_SUBTREE_CONTROL();
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

            fs::path cpu_max(_cgrp_path / CPU_MAX());
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

        bool set_memory_max(unsigned int bytes)
        {
            //  echo "$BYTES" > memory.max

            fs::path memory_max(_cgrp_path / MEMORY_MAX());
            bool success = file_utils::write_text(memory_max, std::to_string(bytes));
            
            return success;
        }

    };
        
}

#endif
