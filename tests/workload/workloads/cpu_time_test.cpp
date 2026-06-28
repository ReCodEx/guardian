#include <iostream>

// Function to consume CPU cycles through computation
void consume_cpu() {
    volatile double result = 0;
    for (int i = 0; i < 1000000; i++) {
        for (int j = 0; j < 1000; j++) {
            result += i * j / (i + 1.0);  // Computationally intensive operation
        }
    }
    std::cout << "Iteration complete: " << result << std::endl;
}

int main() {
    std::cout << "Starting CPU intensive task..." << std::endl;
    consume_cpu();
    return 0;
}