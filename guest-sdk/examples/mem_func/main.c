#include "winios/winios.h"

// Helper function with 8 arguments (all passed in registers X0..X7 according to AAPCS64)
static int test_reg_args(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}

// Helper function with 10 arguments (first 8 in X0..X7, last 2 on stack)
static int test_stack_args(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

// Deeply nested recursive call to exercise call frame push/pop and LR/FP preservation
static int recursive_sum(int n) {
    if (n <= 1) {
        return n;
    }
    return n + recursive_sum(n - 1);
}

// File-scoped memory buffers for freestanding buffer verification
static uint8_t g_src[32];
static uint8_t g_dst[32];

int main(int argc, const char* const* argv) {
    (void)argc;
    (void)argv;

    // 1. Verify 8 register arguments
    int sum8 = test_reg_args(1, 2, 3, 4, 5, 6, 7, 8); // 36
    if (sum8 != 36) {
        return 1;
    }

    // 2. Verify 10 arguments (spill to stack)
    int sum10 = test_stack_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 10); // 55
    if (sum10 != 55) {
        return 2;
    }

    // 3. Verify nested recursive call
    int rsum = recursive_sum(5); // 5+4+3+2+1 = 15
    if (rsum != 15) {
        return 3;
    }

    // 4. Memory buffer operations
    for (int i = 0; i < 32; ++i) {
        g_src[i] = (uint8_t)(i * 3 + 1);
        g_dst[i] = 0;
    }

    winios_memcpy(g_dst, g_src, sizeof(g_src));
    if (winios_memcmp(g_dst, g_src, sizeof(g_src)) != 0) {
        return 4;
    }

    winios_memset(g_dst, 0xAA, 16);
    for (int i = 0; i < 16; ++i) {
        if (g_dst[i] != 0xAA) return 5;
    }
    for (int i = 16; i < 32; ++i) {
        if (g_dst[i] != g_src[i]) return 6;
    }

    // Expected success status: 55
    return 55;
}
