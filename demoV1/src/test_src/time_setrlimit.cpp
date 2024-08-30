#include <iostream>
#include <vector>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/errno.h>

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    unsigned int limit = std::stoi(args[1]);
    rlimit time{.rlim_cur = limit,.rlim_max = limit};
    setrlimit(RLIMIT_CPU, &time);
    while(true)
    {
        std::cout << "xdd" << std::endl;
    }
}