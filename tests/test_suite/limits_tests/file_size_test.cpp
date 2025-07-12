#include <iostream>
#include <fstream>
#include <string>

int main() {
    {
        std::ofstream file("/res/large_file.txt");
        if (!file) {
            std::cerr << "Failed to open file" << std::endl;
            return 1;
        }

        const std::string chunk(1024, 'X');  // 1KB chunk
        // Try to write 1GB
        for(int i = 0; i < 1024; i++) {
            file << chunk;
            if (i % 1024 == 0) {
                std::cout << "Written " << (i/1024) << "MB" << std::endl;
            }
        }
    }
    return 0;
}