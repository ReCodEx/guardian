#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    for(int i = 0; i < 1000; i++) {
        pid_t pid = fork();
        if(pid < 0) {
            std::cerr << "Fork failed after " << i << " processes" << std::endl;
            throw std::runtime_error("Fork failed");
        }
        if(pid == 0) {  // Child process
            sleep(10);  // Keep the process around
            exit(0);
        }
        std::cout << "Created process #" << i << std::endl;
    }
    return 0;
}