#ifndef PROCESS
#define PROCESS

#include <string>
#include <vector>

namespace process_utils
{

/// @brief Convert an std::vector<std::string> to nullptr terminated std::vector<char*>.
/// @param args 
/// @return 
auto convert_to_argv(std::vector<std::string>& args)
{
    std::vector<char*> cstrings{};

    for(auto&& string : args)
        {
            cstrings.emplace_back(string.data());
        }
        cstrings.emplace_back(nullptr);

        return cstrings;
    }
}
#endif
