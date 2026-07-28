#include <cstdio>

// Consumes ~32 MiB of stack: past a typical inherited 8 MiB RLIMIT_STACK, but
// finite. Succeeds only if no stack cap (or an unlimited one) is in force (#22).
// Built at -O0, and each frame is touched, so frames are not elided.
static void consume(const char* prev, int depth) {
    char frame[4096];
    frame[0] = prev ? prev[0] : 1;
    if (depth == 0) {
        std::printf("deep-ok\n");
        return;
    }
    consume(frame, depth - 1);
}

int main() {
    consume(nullptr, 8192);  // 8192 * 4 KiB ~= 32 MiB
    return 0;
}
