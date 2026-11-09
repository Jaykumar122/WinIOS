/*
 * compute_arm64.c - Freestanding deterministic ARM64 Darwin userspace test program
 * Campaign 191-S: REAL-002 (-O0) and REAL-003 (-O2)
 */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef unsigned long size_t;

/* Darwin ARM64 System Call helpers */
static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1; /* SYS_exit */
    __asm__ volatile(
        "svc #0x80"
        :
        : "r"(x0), "r"(x16)
        : "memory"
    );
    __builtin_unreachable();
}

static inline int64_t sys_write(int fd, const void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4; /* SYS_write */
    __asm__ volatile(
        "svc #0x80"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x16)
        : "memory"
    );
    return x0;
}

/* Global data (.data) */
volatile uint64_t g_init_val = 0xCAFEBABE12345678ULL;
volatile int32_t g_call_count = 1;

/* Zero-initialized data (.bss) */
volatile uint64_t g_bss_result;
volatile int32_t g_bss_status;

/* Read-only strings (.rodata) */
static const char kPrefix[] = "COMPUTE_OK: 0x";
static const char kNewline[] = "\n";
static const char kHexChars[] = "0123456789ABCDEF";
static const char kFailPrefix[] = "COMPUTE_FAIL: ";

/* 1. Stack frame loop/recursive computation */
__attribute__((noinline))
uint64_t compute_fib_sum(uint32_t n) {
    uint64_t table[16];
    if (n > 15) n = 15;
    table[0] = 0;
    table[1] = 1;
    uint64_t sum = table[0] + table[1];
    for (uint32_t i = 2; i <= n; i++) {
        table[i] = table[i - 1] + table[i - 2];
        sum += table[i];
    }
    return sum;
}

/* 2. 32-bit & 64-bit integer arithmetic with signed division, modulus, and shifts */
__attribute__((noinline))
int64_t compute_arithmetic(int32_t a, int32_t b, int64_t c, int64_t d) {
    int32_t q32 = a / b;
    int32_t r32 = a % b;
    int64_t q64 = c / d;
    int64_t r64 = c % d;
    uint64_t q32_u64 = (uint64_t)(int64_t)q32;
    uint64_t r32_u64 = (uint64_t)(int64_t)r32;
    uint64_t shift1 = q32_u64 << 24;
    uint64_t shift2 = r32_u64 << 16;
    uint64_t combined = shift1 ^ shift2;
    return (int64_t)combined + q64 - r64;
}

/* 3. Function pointer call (indirect branch BLR) */
typedef uint64_t (*binary_op_t)(uint64_t, uint64_t);

__attribute__((noinline))
uint64_t op_xor_add(uint64_t x, uint64_t y) {
    return (x ^ y) + 0x5555ULL;
}

__attribute__((noinline))
uint64_t apply_operator(binary_op_t op, uint64_t x, uint64_t y) {
    return op(x, y);
}

/* Helper to print 64-bit hex value */
static void print_hex64(uint64_t val) {
    char buf[16];
    for (int i = 15; i >= 0; --i) {
        buf[i] = kHexChars[val & 0xFu];
        val >>= 4;
    }
    sys_write(1, buf, 16);
}

/* Main driver function */
__attribute__((noinline))
int compute_main(void) {
    /* Step 1: Fibonacci loop */
    uint64_t fib_res = compute_fib_sum(10);
    if (fib_res != 0x8FULL) {
        sys_write(2, kFailPrefix, sizeof(kFailPrefix) - 1);
        sys_write(2, "FIB\n", 4);
        return 1;
    }

    /* Step 2: Arithmetic with signed division and modulus */
    int64_t arith_res = compute_arithmetic(-105, 10, 1000000000LL, -7LL);
    if (arith_res != 0x1772C44LL) {
        sys_write(2, kFailPrefix, sizeof(kFailPrefix) - 1);
        sys_write(2, "ARITH\n", 6);
        return 2;
    }

    /* Step 3: Function pointer call (BLR) */
    uint64_t op_res = apply_operator(op_xor_add, g_init_val, fib_res);
    if (op_res != 0xCAFEBABE1234AC4CULL) {
        sys_write(2, kFailPrefix, sizeof(kFailPrefix) - 1);
        sys_write(2, "OP\n", 3);
        return 3;
    }

    /* Step 4: Write to BSS and compute final checksum */
    g_bss_result = ((uint64_t)arith_res) ^ op_res;
    uint64_t checksum = g_bss_result + (uint64_t)g_call_count;
    if (checksum != 0xCAFEBABE13438009ULL) {
        sys_write(2, kFailPrefix, sizeof(kFailPrefix) - 1);
        sys_write(2, "CHECKSUM\n", 9);
        return 4;
    }

    /* Print success banner: "COMPUTE_OK: 0xCAFEBABE13438009\n" */
    sys_write(1, kPrefix, sizeof(kPrefix) - 1);
    print_hex64(checksum);
    sys_write(1, kNewline, sizeof(kNewline) - 1);

    return 0;
}

/* Entry point: called by Darwin userspace entry */
__attribute__((naked))
void start(void) {
    __asm__ volatile(
        "bl _compute_main\n"
        "mov x16, #1\n" /* SYS_exit */
        "svc #0x80\n"
    );
}
