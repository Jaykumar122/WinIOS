#include "winios/winios.h"

// Arithmetic and Control-Flow Guest Workload
// Verifies integer arithmetic, bitwise logic, conditional branches, loops, and boundary values.
int main(int argc, const char* const* argv) {
    (void)argc;
    (void)argv;

    // 1. Basic arithmetic
    int a = 120;
    int b = 45;
    int sum = a + b;       // 165
    int diff = a - b;      // 75
    int prod = a * 3;      // 360
    int quot = a / 3;      // 40

    if (sum != 165 || diff != 75 || prod != 360 || quot != 40) {
        return 1;
    }

    // 2. Bitwise operations and shifts
    uint32_t flags = 0xAA55AA55;
    uint32_t mask = 0x00FF00FF;
    uint32_t masked = flags & mask;   // 0x00550055
    uint32_t shifted = masked << 4;   // 0x05500550
    uint32_t xored = shifted ^ 0x05500550; // 0

    if (xored != 0) {
        return 2;
    }

    // 3. Loops and conditional execution (Fibonacci sequence)
    uint32_t f0 = 0;
    uint32_t f1 = 1;
    for (int i = 2; i <= 10; ++i) {
        uint32_t next = f0 + f1;
        f0 = f1;
        f1 = next;
    }
    // Fib(10) is 55
    if (f1 != 55) {
        return 3;
    }

    // 4. Boundary value calculations
    uint32_t max_u32 = 0xFFFFFFFF;
    uint32_t wrapped = max_u32 + 1; // 0 modulo 2^32
    if (wrapped != 0) {
        return 4;
    }

    // Expected success status: 42
    return 42;
}
