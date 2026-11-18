// Freestanding ARM64 Guest Test Workload for Campaign 218
// Pure C, zero libc dependencies

__attribute__((noinline))
static int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

__attribute__((noinline))
static int compute_math(int a, int b) {
    int res = (a * 3) + (b >> 1);
    res ^= 0x5A;
    return res;
}

__attribute__((noinline))
static int test_memory_ops(void) {
    volatile int array[4];
    array[0] = 10;
    array[1] = 20;
    array[2] = 30;
    array[3] = 40;
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        sum += array[i];
    }
    return sum; // 100
}

int entry_point(void) {
    // 1. Recursive call test: factorial(5) = 120
    int f = factorial(5);

    // 2. Arithmetic & bitwise math: (10 * 3) + (8 >> 1) ^ 0x5A = 34 ^ 90 = 120
    int m = compute_math(10, 8);

    // 3. Stack memory load/store test: sum = 100
    int s = test_memory_ops();

    // 4. Combined deterministic result: 120 + 120 + 100 = 340
    return f + m + s;
}
