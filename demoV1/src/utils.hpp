#ifndef UTILS
#define UTILS

#include <string>
#include <vector>
#include <boost/tokenizer.hpp>
#include <boost/optional.hpp>
#include <set>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <format>

namespace type_utils
{
    template<typename T>
    inline constexpr auto to_std_optional(boost::optional<T> opt) 
    {
        if (opt.has_value()) 
        {
            return std::make_optional(std::forward<decltype(opt)>(opt).value());
        }
        else 
        {
            return std::optional<T>();
        }
    };
}

namespace string_utils
{

    inline std::vector<std::string> split(const std::string& str)
    {
        boost::tokenizer<> tok(str);
        std::vector<std::string> res;
        for(boost::tokenizer<>::iterator it = tok.begin(); it != tok.end(); ++it)
        {
            res.push_back(*it);
        }
        return res;
    }
    
}

namespace file_utils
{
    namespace fs = std::filesystem;

    void list_directory(const fs::path& path)
    {
        for (const auto & entry : fs::directory_iterator(path))
            std::cout << entry.path() << std::endl;
    }

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

    template<typename ... Args>
    bool append_formatted(const fs::path& path, const std::format_string<Args...> fmt, Args&&... args)
    {
        return append_text(path, std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    void print_file(const fs::path& path)
    {
        std::fstream f(path);
        
        if(f.is_open())
        {
            std::cout << f.rdbuf();
        }
    }

    void print_lines(const fs::path& path)
    {
        std::fstream f(path);
        std::string line;
        while(f)
        {
            getline(f, line);
            std::cout << line;
        }
    }

    std::ifstream& skip_lines(std::ifstream &is, std::streamsize n)
    {
        while(is.good() && n--)
        {
            is.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        return is;
    }

    std::string read_row_col(std::ifstream& f, unsigned int row, unsigned int col)
    {
        auto& s = skip_lines(f, row);

        std::string line;
        getline(s, line);
        std::stringstream ss(line);
    
        std::string word;
        while (!ss.eof() && col-- >= 0) 
        {
            ss >> word;
        }
        return std::move(word);
    }
    
}

#endif 