#include <iostream>
#include <vector>
#include <fstream>


int main(int argc, char ** argv)
{
    std::vector<std::string> args(argv+1, argv + argc);
    std::ofstream file("hello.txt");
    file << "Hello World from contained process #" << args[0] << std::endl;
}