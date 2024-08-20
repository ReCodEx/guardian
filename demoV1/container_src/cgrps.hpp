#ifndef CGRPS
#define CGRPS

#include <vector>
#include <string>
#include <set>
#include <fstream>

#include "utils.hpp"

namespace cgrp_management {
    constexpr std::string cgroup_path = "/sys/fs/cgroup";

    class cntrlr_supervisor
    {
        inline static std::string cntrls_file = "/cgroup.controllers";
        static std::set<std::string> _cntrls;
    public:
        cntrlr_supervisor()
        {
            load_available();
        }

        void load_available()
        {
            std::ifstream cntrls(cntrls_path());
            std::string line;
            getline(cntrls, line);
            _cntrls = string_utils::split_to_set(line);
        }

        static void list_available()
        {
            std::ifstream cntrls(cntrls_path());
            std::string line;
            getline(cntrls, line);

            std::cout << line << std::endl;
        }

    private:
        static std::string cntrls_path()
        {
            return cgroup_path + cntrls_file;
        }
    };

    class cgrpv2_manager
    {
        cntrlr_supervisor _cntrl_sup;
    public:

    private:

    };


    

}

#endif
