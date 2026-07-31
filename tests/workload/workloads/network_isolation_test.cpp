#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <cstring>

int main() {
    // Try to create a socket and connect to an external address
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cout << "Socket creation failed (expected when networking is disabled)\n";
        return 0; // This is actually success in our case
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(80);
    
    // Try to connect to a public IP (8.8.8.8 - Google DNS)
    inet_pton(AF_INET, "8.8.8.8", &serv_addr.sin_addr);
    
    int result = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    close(sock);

    if (result >= 0) {
        std::cout << "Network connection succeeded but should be blocked\n";
        return 1;
    }

    std::cout << "Network isolation test passed\n";
    return 0;
}