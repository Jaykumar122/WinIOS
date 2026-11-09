/*
 * main_with_dylib.c - Freestanding real ARM64 Darwin binary dynamically linked to libmathreal.dylib
 * Campaign 191-S: REAL-005
 */

typedef unsigned char uint8_t;
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

/* External declarations from libmathreal.dylib */
extern int32_t real_add(int32_t a, int32_t b);
extern uint64_t real_read_global(void);

/* Strings in .rodata */
static const char kPrefix[] = "DYLIB_OK: 0x";
static const char kNewline[] = "\n";
static const char kHexChars[] = "0123456789ABCDEF";
static const char kFail[] = "DYLIB_FAIL\n";

/* Hex formatter */
static void print_hex64(uint64_t val) {
    char buf[16];
    for (int i = 15; i >= 0; --i) {
        buf[i] = kHexChars[val & 0xFu];
        val >>= 4;
    }
    sys_write(1, buf, 16);
}

/* Main logic */
__attribute__((noinline))
int dylib_main(void) {
    /* Step 1: Call real_add cross-binary */
    int32_t sum = real_add(40, 2);
    if (sum != 42) {
        sys_write(2, kFail, sizeof(kFail) - 1);
        return 1;
    }

    /* Step 2: Call real_read_global cross-binary */
    uint64_t magic = real_read_global();
    if (magic != 0xA11C001CAFEBABEULL) {
        sys_write(2, kFail, sizeof(kFail) - 1);
        return 2;
    }

    /* Step 3: Compute final combined token: 0xA11C001CAFEBABE + 42 = 0xA11C001CAFEBAE8 */
    uint64_t token = magic + (uint64_t)sum;
    if (token != 0xA11C001CAFEBAE8ULL) {
        sys_write(2, kFail, sizeof(kFail) - 1);
        return 3;
    }

    /* Print success banner: "DYLIB_OK: 0x0A11C001CAFEBAE8\n" */
    sys_write(1, kPrefix, sizeof(kPrefix) - 1);
    print_hex64(token);
    sys_write(1, kNewline, sizeof(kNewline) - 1);

    return 0;
}

/* Entry point */
__attribute__((naked))
void start(void) {
    __asm__ volatile(
        "bl _dylib_main\n"
        "mov x16, #1\n" /* SYS_exit */
        "svc #0x80\n"
    );
}
