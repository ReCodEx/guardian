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
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    for(int i = 0; i < bytes; i+=1024)
    {
        void* ptr = malloc(1024);
        *(char*)ptr = 'x';
        std::cout << std::format("Allocation #{} out of {}", i, bytes/1024) << std::endl;
        //std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << std::format("Survived allocating and writing {} bytes", bytes) << std::endl;
}