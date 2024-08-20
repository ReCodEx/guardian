#ifndef UTILS
#define UTILS

#include <string>
#include <vector>
#include <boost/tokenizer.hpp>
#include <set>


namespace string_utils
{

    inline std::set<std::string> split_to_set(const std::string& str)
    {
        boost::tokenizer<> tok(str);
        std::set<std::string> res;
        for(boost::tokenizer<>::iterator it = tok.begin(); it != tok.end(); ++it)
        {
            res.insert(*it);
        }
    }
    
}

#endif 