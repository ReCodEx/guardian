// Prints its effective UID and exits 0. Used as a setuid-root probe to prove a
// mount's MS_NOSUID actually strips the setuid bit: if the bit were honored the
// probe would run with euid 0; under nosuid it keeps the caller's (box) uid.
#include <unistd.h>

#include <cstdio>

int main() {
    std::printf("%u\n", geteuid());
    return 0;
}
