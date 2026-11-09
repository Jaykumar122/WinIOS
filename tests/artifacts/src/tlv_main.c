#include <stdint.h>

__thread int g_val = 123;

static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1;
    __asm__ volatile ("svc #0x80" : : "r"(x0), "r"(x16) : "memory");
}

static inline int64_t sys_write(int fd, const void* buf, uint64_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register uint64_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4;
    __asm__ volatile ("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory");
    return x0;
}

void real_start(void) {
    if (g_val == 123) {
        g_val = 456;
        if (g_val == 456) {
            sys_write(1, "TLV_OK: 456\n", 12);
            sys_exit(0);
        }
    }
    sys_write(1, "TLV_ERR\n", 8);
    sys_exit(1);
}

__attribute__((naked))
void start(void) {
    __asm__ volatile (
        "bl _real_start\n"
        "mov x0, #1\n"
        "mov x16, #1\n"
        "svc #0x80\n"
    );
}
