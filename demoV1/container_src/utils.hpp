#ifndef UTILS
#define UTILS

#include <string>
#include <vector>
#include <boost/tokenizer.hpp>
#include <set>
#include <fstream>


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
        return res;
    }
    
}

namespace file_utils
{
    bool append_text(const std::string& path, const std::string& data)
    {
        std::ofstream file(path, std::ios_base::app);
        if(file.is_open())
        {
            file << data;

            return true;
        }
        else
        {
            return false;
        }
    }

    bool write_text(const std::string& path, const std::string& data)
    {
        std::ofstream file(path);
        if(file.is_open())
        {
            file << data;
            file.close();
            if(file.good())
            {
                return true;
            }
        }
        return false;
    }
    
}

#endif 