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
#include <optional>
#include <string_view>

#include <sys/types.h>
#include <unistd.h>
#include <cerrno>

#include "terminate.hpp"

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

/// @brief Metric-unit conversions shared by both metadata writers (the compat
/// meta-file in meta_file.hpp and the standalone stats-yaml in config.hpp), so
/// the two output paths can never drift on units again. Isolate's conventions:
/// times are `seconds.milliseconds` (3 decimals, integer-derived), memory is
/// binary KB (bytes >> 10).
namespace units
{
    /// @brief Format a millisecond count as Isolate's `S.mmm` seconds string.
    inline std::string sec_from_ms(size_t ms)
    {
        return std::format("{}.{:03}", ms / 1000, ms % 1000);
    }

    /// @brief Format a microsecond count as Isolate's `S.mmm` seconds string
    /// (truncating to millisecond resolution, as Isolate does).
    inline std::string sec_from_usec(size_t usec)
    {
        return sec_from_ms(usec / 1000);
    }

    /// @brief Bytes to binary KB (KiB), matching Isolate's `mem >> 10`.
    inline size_t bytes_to_kib(size_t bytes) { return bytes >> 10; }
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

    /// @brief Read a numeric value by key from a cgroup "key value" stat file
    /// (one `key value` pair per line, e.g. memory.events / cpu.stat).
    /// @return the value for @p key, or nullopt if the file is unreadable or the
    /// key is absent. Robust to field reordering/additions across kernels —
    /// unlike a fixed row index. See issue #18 for migrating other readers here.
    inline std::optional<size_t> read_keyed_size(const fs::path& path, std::string_view key)
    {
        std::ifstream f(path);
        if (!f)
        {
            return std::nullopt;
        }
        std::string k;
        size_t v = 0;
        while (f >> k >> v)
        {
            if (k == key)
            {
                return v;
            }
        }
        return std::nullopt;
    }

    /// @brief Recursively set owner/group across a directory tree WITHOUT
    /// following symlinks, for the box ownership dance (chown box/ to the box
    /// user before a run, back to the caller after).
    /// @details The tree's contents may be attacker-influenced — the untrusted
    /// task writes into box/ — so we must never dereference a symlink it left:
    /// `lchown` retargets the link itself, and `recursive_directory_iterator`
    /// does not descend through directory symlinks by default. Special files
    /// (fifos/sockets/symlinks) are lchown'd in place, not unlinked: no-follow
    /// is the load-bearing property, `chown` already strips setuid/setgid bits
    /// off regular files, and the box user cannot create device nodes (no
    /// CAP_MKNOD). Runs as root with no live task and the box flock held, so no
    /// concurrent mutator races the walk. Terminates on any failure.
    inline void lchown_tree(const fs::path& root, uid_t uid, gid_t gid)
    {
        auto set_owner = [&](const fs::path& p) {
            if (::lchown(p.c_str(), uid, gid) < 0)
            {
                terminate("Cannot lchown {}: errno {}", p.string(), errno);
            }
        };

        set_owner(root);  // the iterator yields contents, not root itself
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            set_owner(entry.path());
        }
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