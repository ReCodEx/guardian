#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>



int main(int argc, char ** argv)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));

    std::vector<std::string> args(argv, argv + argc);
    //std::cout << args[0] << "\n";
    std::ofstream file("/home/simonkurz/mff/rcdx_cntnr/demoV1/hello.txt");
    file << args[0] << std::endl;
}