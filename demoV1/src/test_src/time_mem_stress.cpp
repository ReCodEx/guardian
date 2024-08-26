#include <iostream>

unsigned long long factorial(int x)
{
    if(x==2) return 2;
    return x*factorial(x-1);
}

int main(int argc, char ** argv)
{
    auto x = factorial(50);
    std::cout << x << std::endl;
}