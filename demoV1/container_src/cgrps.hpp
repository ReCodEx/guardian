#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>

#include "utils.hpp"

namespace cgrp_management 
{
    namespace fs = std::filesystem;
    constexpr std::string_view CGRP_FS_PATH_ = "/sys/fs/cgroup";
    constexpr std::string_view CGROUP_CONTROLLERS_ = "/cgroup.controllers";
    constexpr std::string_view CGROUP_SUBTREE_CONTROL_ = "/cgroup.subtree_control";
    constexpr std::string_view CPU_MAX_ = "/cpu.max";

    inline std::string const& CGRP_FS_PATH()
    {
        static std::string path = "/sys/fs/cgroup";
        return path;
    }

    inline std::string const& CGROUP_CONTROLLERS()
    {
        static std::string name = "/cgroup.controllers";
        return name;
    }

    inline std::string const& CGROUP_SUBTREE_CONTROL()
    {
        static std::string name = "/cgroup.subtree_control";
        return name;
    }

    inline std::string const& CPU_MAX()
    {
        static std::string name = "/cpu.max";
        return name;
    }


    class cgroupv2_t
    {
        const fs::path _cgrp_path;
    public:
        cgroupv2_t(const std::string& rel_cgrp_path) : _cgrp_path(CGRP_FS_PATH() + rel_cgrp_path)
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
            std::ifstream cntrls(_cgrp_path.string() + "/cpu.max");
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

            std::string subtree_control = _cgrp_path + CGROUP_SUBTREE_CONTROL();
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

        bool set_cpu_max(int out_of_100000)
        {
            if(out_of_100000 < 1000) return false;

            std::string cpu_max_file(_cgrp_path + CPU_MAX());

            std::ostringstream val;
            val << out_of_100000 << " " << 100000;
            bool success = file_utils::write_text(cpu_max_file, val.str());

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

    };
        
}

#endif
