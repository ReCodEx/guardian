#include <stdlib.h>

int main(int argc, char ** argv)
{
    int time = 2000;
    int mem = 1 << 20;
    int iters = 1000000;
    for(int i = 0; i < iters; ++i)
    {
        void* ptr = malloc(1 << iters);
        free(ptr);
    }
}