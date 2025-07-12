#include <chrono>
#include <thread>
#include <iostream>

int main() {
    // Run an infinite loop that will be terminated by the wall time limit
    int i = 0;
    while (i++ < 30) {
        // Do some work and print to show activity
        std::cout << "Running..." << std::endl;
        // Sleep briefly to not consume 100% CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}