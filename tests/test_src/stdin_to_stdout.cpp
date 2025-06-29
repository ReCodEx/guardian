#include <iostream>
#include <fstream>

int main(int argc, char** argv)
{
    std::string line;
    std::cout << "Printing lines from stdin to stdout:" << std::endl;
    std::ofstream f("/tests/testfile.txt");
    while(getline(std::cin, line))
    {
        std::cout << line << std::endl;
        f << line << std::endl;
    }
    std::cout << "Done" << std::endl;
    std::cerr << "Testing stderr redirection." << std::endl;
    std::exit(0);
}