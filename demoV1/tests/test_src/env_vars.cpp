#include <iostream>
#include <fstream>
#include <format>

int main(int argc, char** argv)
{
    std::string line;
    std::cout << "Printing environment variables to stdout:" << std::endl;
    std::cout << std::format("The value of \"HELLO\" is \"{}\".", std::getenv("HELLO")) << std::endl;
    std::cout << std::format("The value of \"WORLD\" is \"{}\".", std::getenv("WORLD")) << std::endl;
    std::cout << std::format("The value of \"PATH\" is \"{}\".", std::getenv("PATH")) << std::endl;
    std::cout << "Done" << std::endl;
    std::exit(0);
}