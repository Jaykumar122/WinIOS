/*
 * ios_chain_app.c - Real iOS-target ARM64 executable with NONZERO rebases and binds
 * Campaign 191-V Stage V0
 *
 * Exercises:
 *  - Pointer-to-global (gp_counter -> &g_counter) [REBASE]
 *  - Global function pointer array (g_fn_table[2] = {local_alpha, local_beta}) [2x REBASE]
 *  - Global pointer + GOT import from real iOS dylib (/usr/lib/libioschain.dylib) [BIND(s)]
 */

typedef unsigned char      uint8_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;

static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1;
    __asm__ volatile("svc #0x80" : : "r"(x0), "r"(x16) : "memory");
    __builtin_unreachable();
}

static inline int64_t sys_write(int fd, const void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return x0;
}

extern int32_t ios_chain_add(int32_t a, int32_t b);

/* Local functions referenced by global pointer array (emit rebase fixups) */
__attribute__((noinline))
int32_t local_alpha(int32_t x) {
    return x + 7;
}

__attribute__((noinline))
int32_t local_beta(int32_t x) {
    return x * 2;
}

typedef int32_t (*fn1_t)(int32_t);
typedef int32_t (*fn2_t)(int32_t, int32_t);

/* Global object + pointer-to-global + function-pointer table + imported function pointer */
int32_t g_counter = 100;
int32_t* volatile gp_counter = &g_counter;
fn1_t volatile g_fn_table[2] = { local_alpha, local_beta };
fn2_t volatile g_ext_fn = ios_chain_add;

static void write_dec(int32_t v) {
    char buf[16];
    int idx = 0;
    if (v == 0) {
        buf[idx++] = '0';
    } else {
        uint32_t uv = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
        if (v < 0) sys_write(1, "-", 1);
        char rev[16];
        int r = 0;
        while (uv > 0) {
            rev[r++] = (char)('0' + (uv % 10u));
            uv /= 10u;
        }
        while (r > 0) buf[idx++] = rev[--r];
    }
    sys_write(1, buf, (size_t)idx);
}

__attribute__((noinline))
int chain_main(void) {
    /* Force non-folded indirect reads via volatile globals */
    int32_t c = *gp_counter;            /* 100 */
    int32_t v1 = g_fn_table[0](c);      /* 100 + 7 = 107 */
    int32_t v2 = g_fn_table[1](v1);     /* 107 * 2 = 214 */
    int32_t v3 = g_ext_fn(v2, 9);       /* 214*3 + 9 + 5 = 656 */
    int32_t v4 = g_ext_fn(4, 6);        /* 4*3 + 6 + 5 = 23 */

    sys_write(1, "IOS_CHAIN: ", 11);
    write_dec(v1); sys_write(1, ",", 1);
    write_dec(v2); sys_write(1, ",", 1);
    write_dec(v3); sys_write(1, ",", 1);
    write_dec(v4); sys_write(1, "\n", 1);

    return (v1 == 107 && v2 == 214 && v3 == 656 && v4 == 23) ? 0 : 1;
}

__attribute__((naked))
void start(void) {
    __asm__ volatile(
        "bl _chain_main\n"
        "mov x16, #1\n" /* SYS_exit */
        "svc #0x80\n"
    );
}
