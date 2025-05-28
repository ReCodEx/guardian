#include "utils.hpp"

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    for(const auto& arg : args)
    {
        std::cout << "Listing directory contents of " << arg << ":" << std::endl;
        file_utils::list_directory(arg);
    }
}