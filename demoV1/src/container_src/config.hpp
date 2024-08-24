#ifndef CONFIG
#define CONFIG

#include "cgrps.hpp"
#include "namespaces.hpp"

namespace config
{
    namespace cgrp = cgrp_management;

    struct main_config
    {
        cgrp::cgrp_config _cgrp;

    };


    class config_parser
    {
    public:
        
    };

}



#endif