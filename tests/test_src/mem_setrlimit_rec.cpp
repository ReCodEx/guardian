#include <iostream>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/errno.h>
#include <vector>
#include <format>

unsigned long long infinite(int x)
{
    if(x==2) return 2;
    return x*infinite(x);
}

int main(int argc, char ** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    unsigned int limit = std::stoi(args[1]);

    std::cout << std::format("Setting memory limit to {} bytes", limit) << std::endl;
    rlimit mem{.rlim_cur = limit,.rlim_max = limit};
    int rv = setrlimit(RLIMIT_AS, &mem);
    auto x = infinite(50);
    std::cout << x << std::endl;
}