#include <iostream>

void recursive_stack_consumer(char arr[4096], int depth = 0) {
    if (depth > 100) {
        return;
    }
    // Create a large array on the stack
    char new_arr[4096];
    // Do something with the array to prevent optimization
    new_arr[0] = arr[0];
    // Recurse until we run out of stack space
    recursive_stack_consumer(new_arr, depth + 1);
}

int main() {
    char initial[4096] = {0};
    recursive_stack_consumer(initial);
    return 0;
}