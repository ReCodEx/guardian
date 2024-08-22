#ifndef UTILS
#define UTILS

#include <string>
#include <vector>
#include <boost/tokenizer.hpp>
#include <set>
#include <fstream>
#include <filesystem>


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
    namespace fs = std::filesystem;

    bool append_text(const fs::path& path, const std::string& data)
    {
        std::ofstream file(path, std::ios_base::app);
        file << data;
        file.close();
        return file.good();
    }

    bool write_text(const fs::path& path, const std::string& data)
    {
        std::ofstream file(path);
        file << data;
        file.close();
        return file.good();
    }

    template<typename ... Args>
    bool write_formatted(const fs::path& path, const std::format_string<Args...> fmt, Args&&... args)
    {
        return write_text(path, std::vformat(fmt.get(), std::make_format_args(args...)));
    }
    
}

#endif 