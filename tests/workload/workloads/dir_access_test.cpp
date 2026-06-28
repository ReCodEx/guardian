#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

int main() {
    // Try to access allowed directory
    std::filesystem::path allowed_dir = "/allowed";
    if (!std::filesystem::exists(allowed_dir)) {
        std::cout << "Allowed directory not accessible\n";
        return 1;
    }

    // Try to write to allowed directory
    std::ofstream test_file(allowed_dir / "test.txt");
    if (!test_file) {
        std::cout << "Cannot write to allowed directory\n";
        return 1;
    }
    test_file.close();

    // Try to access restricted directory
    std::filesystem::path restricted_dir = "/home";
    if (std::filesystem::exists(restricted_dir)) {
        try {
            for(auto const& entry : std::filesystem::directory_iterator{restricted_dir}) {
                std::cout << "Should not be able to access " << entry.path() << "\n";
                return 1;
            }
        } catch(...) {
            // Expected - we should not have access
        }
    }

    std::cout << "Directory access test passed\n";
    return 0;
}