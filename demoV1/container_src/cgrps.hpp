#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>

#include "utils.hpp"

namespace cgrp_management {
    namespace fs = std::filesystem;
    constexpr std::string CGRP_PATH = "/sys/fs/cgroup";


    class cgroupv2_t
    {
        const fs::path _cgrp_path;
    public:
        cgroupv2_t(const std::string& rel_cgrp_path) : _cgrp_path(CGRP_PATH + rel_cgrp_path)
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
        inline static std::string _cntrls_file = "/cgroup.controllers";
        const std::string& _cgrp_rel_path;
        
        static std::string cntrls_path()
        {
            return CGRP_PATH + _cntrls_file;
        }
    public:
        cntrlr_operator(const std::string& path) : _cgrp_rel_path(path) {}

        static void cat_cntrls()
        {
            std::ifstream cntrls(cntrls_path());
            if(cntrls.is_open())
            {
                std::cout << cntrls.rdbuf() << std::endl;
            }
        }

    private:


        
    };

    class cpu_cntrlr_operator : public cntrlr_operator
    {
    public:

    };

    class memory_cntrlr_operator : public cntrlr_operator
    {

    };

    class cgrpv2_manager
    {
    public:
        cgrpv2_manager()
        {

        }

        ~cgrpv2_manager()
        {

        }
    private:
        
    };


    

}

#endif
