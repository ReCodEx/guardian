#include <iostream>
#include <fstream>

int main(int argc, char** argv)
{
    std::ofstream large_file("/temp/large_file");
    large_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    for(int i = 0; i < 2; i++)
    {
        large_file << i << std::endl;
    }
}
