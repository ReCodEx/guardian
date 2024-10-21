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
    //rlimit stack{.rlim_cur = 64*1024,.rlim_max = 64*1024};
    //int rvstack = setrlimit(RLIMIT_STACK, &stack);
        
    std::vector<std::string> args(argv, argv + argc);
    unsigned int limit = std::stoi(args[1]);
    unsigned int bytes = std::stoi(args[2]);

    std::cout << std::format("Setting memory limit to {} bytes", limit) << std::endl;
    rlimit mem{.rlim_cur = limit,.rlim_max = limit};
    int rv = setrlimit(RLIMIT_AS, &mem);

    void* ptr = malloc(bytes);
    if(ptr)
        std::cout << std::format("Succesfully allocated {} bytes", bytes) << std::endl;

    for(int i = 0; i < bytes; i+=8*16)
    {
        *(((char*)ptr) + i) = 'x';
        std::cout << std::format("Byte #{} out of {}", i, bytes) << std::endl;
    }
    free(ptr);
    std::cout << std::format("Survived allocating and touching {} bytes", bytes) << std::endl;
}