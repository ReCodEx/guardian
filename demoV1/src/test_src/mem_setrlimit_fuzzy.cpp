#include <sys/time.h>
#include <sys/resource.h>
#include <sys/errno.h>
#include <format>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    unsigned int limit = std::stoi(args[1]);
    unsigned int bytes = std::stoi(args[2]);

    std::cout << std::format("Setting memory limit to {} bytes", limit) << std::endl;
    rlimit mem{.rlim_cur = limit,.rlim_max = limit};
    std::cout << mem.rlim_max << std::endl;
    int rv = setrlimit(RLIMIT_AS, &mem);
    if(rv !=0)
    {
        std::cout << std::format("Failed to set memory limit for the child process. RV: {}, Errno: {}", rv,errno);
    }
    //std::this_thread::sleep_for(std::chrono::seconds(60));
    int size = 4096;
    for(int i = 0; i < bytes; i+=size)
    {
        void* ptr = malloc(size);
        *(char*)ptr = 'x';
        std::cout << std::format("Allocation #{} out of {}", i, bytes/size) << std::endl;
        //std::this_thread::sleep_for(std::chrono::milliseconds(5));
        //free(ptr);
    }
    std::cout << std::format("Survived allocating and writing {} bytes with a limit of {}", bytes, limit) << std::endl;
}