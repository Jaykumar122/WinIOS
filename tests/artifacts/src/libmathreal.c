/*
 * libmathreal.c - Freestanding real ARM64 Darwin dynamic library
 * Campaign 191-S: REAL-004
 */

typedef unsigned long long uint64_t;
typedef signed int int32_t;

/* Global internal data inside dylib */
volatile uint64_t g_real_magic = 0xA11C001CAFEBABEULL;

/* Exported function 1: 32-bit addition */
__attribute__((visibility("default"), noinline))
int32_t real_add(int32_t a, int32_t b) {
    return a + b;
}

/* Exported function 2: Read global state */
__attribute__((visibility("default"), noinline))
uint64_t real_read_global(void) {
    return g_real_magic;
}
