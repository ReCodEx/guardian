#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    // Check if specified env var exists and has correct value
    const char* test_var = getenv("TEST_VAR");
    if (!test_var || std::string(test_var) != "test_value") {
        std::cout << "TEST_VAR not found or incorrect value\n";
        return 1;
    }

    // Check that blocked env var doesn't exist
    if (getenv("PATH")) {
        std::cout << "PATH exists but shouldn't\n";
        return 1;
    }

    std::cout << "Environment variables test passed\n";
    return 0;
}