#include <iostream>
#include <fstream>
#include <format>
#include <unistd.h>

int main(int argc, char** argv)
{
    std::cout << getuid() << std::endl;
    for(int i = 0; i < 100; i++)
    {
        std::ofstream file(std::format("/temp/file{}", i));
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        for(int j = 0; j < 2 << 10; ++j)
        {
            file << j << std::endl;
        }
    }
    std::ofstream small_file("/temp/small_file");
    small_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    for(int i = 0; i < 2; i++)
    {
        small_file << i << std::endl;
    }
}
