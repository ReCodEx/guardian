#include <cstdlib>
#include <memory>
#include <vector>
#include <iostream>
#include <fstream>
#include <format>
#include <chrono>
#include <thread>


int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    int bytes = std::stoi(argv[1]);
    void* ptr = std::aligned_alloc(1024, bytes);
    std::cout << std::format("Survived allocating {} bytes", bytes) << std::endl;
    for(int i = 0; i < bytes; i+=8*16)
    {
        *(((char*)ptr) + i) = 'x';
        std::cout << std::format("Byte #{} out of {}", i, bytes) << std::endl;
    }
    free(ptr);
    std::cout << std::format("Survived allocating and reading {} bytes", bytes) << std::endl;
}