#ifndef UTILS
#define UTILS

#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <format>

namespace string_utils
{

    /// @brief Split a directory-rule option list on ':' (e.g. "rw:noexec"),
    /// dropping empty tokens. This is the only delimiter Isolate's dir-rule
    /// options use.
    inline std::vector<std::string> split(const std::string& str)
    {
        std::vector<std::string> res;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, ':'))
        {
            if (!token.empty())
            {
                res.emplace_back(token);
            }
        }
        return res;
    }

}

namespace file_utils
{
    namespace fs = std::filesystem;
    
    inline bool is_prefix(const fs::path& prefix, const fs::path& path)
    {
        fs::path rel = path.lexically_relative(prefix);
        return !rel.empty() && rel.string()[0] != '.';
    }

    inline bool is_valid_path(const fs::path& p)
    {
        try 
        {
            auto normalized = p.lexically_normal();
            return true;
        } 
        catch (...) 
        {
            return false;
        }
    }

    inline bool is_subdirectory(const std::filesystem::path& relative)
    {
        return relative.lexically_relative(".") == relative.string();
    }

    inline void list_directory(const fs::path& path)
    {
        for (const auto & entry : fs::directory_iterator(path))
            std::cout << entry.path() << std::endl;
    }

    inline bool append_text(const fs::path& path, const std::string& data)
    {
        std::ofstream file(path, std::ios_base::app);
        file << data;
        file.close();
        return file.good();
    }

    inline bool write_text(const fs::path& path, const std::string& data)
    {
        std::ofstream file(path);
        file << data;
        file.close();
        return file.good();
    }

    template<typename ... Args>
    bool write_formatted(const fs::path& path, const std::format_string<Args...>& fmt, Args&&... args)
    {
        return write_text(path, std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    template<typename ... Args>
    bool append_formatted(const fs::path& path, const std::format_string<Args...>& fmt, Args&&... args)
    {
        return append_text(path, std::vformat(fmt.get(), std::make_format_args(args...)));
    }

    inline void print_file(const fs::path& path)
    {
        std::fstream f(path);
        
        if(f.is_open())
        {
            std::cout << f.rdbuf();
        }
    }

    inline void print_lines(const fs::path& path)
    {
        std::fstream f(path);
        std::string line;
        while(f)
        {
            getline(f, line);
            std::cout << line;
        }
    }

    inline std::ifstream& skip_lines(std::ifstream &is, std::streamsize n)
    {
        while(is.good() && n--)
        {
            is.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        return is;
    }

    inline std::string read_row_col(std::ifstream& f, size_t row, unsigned int col)
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
        return word;
    }
    
}

#endif 