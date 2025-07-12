#include <iostream>
#include <fstream>
#include <vector>
#include <format>

int main(int argc, char** argv) {
    std::vector<std::ofstream> files;
    // try {
        // Try to open many files simultaneously
        for(int i = 0; i < 100; i++) {
            auto f = std::ofstream(std::format("test{}.txt", i));
            f.exceptions(std::ofstream::failbit | std::ofstream::badbit); // Enable exceptions
            f << "This is test file number " << i << std::endl;
            files.emplace_back(std::move(f));
        }
    // } catch (const std::ios_base::failure& e) {
    //     std::cerr << "I/O operation failed: " << e.what() << std::endl;
    //     return 1;
    // } catch (const std::exception& e) {
    //     std::cerr << "Failed to open file: " << e.what() << std::endl;
    //     return 1;
    // }
    return 0;
}