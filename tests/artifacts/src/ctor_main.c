#include <stdint.h>

extern int g_sequence;
extern int get_leaf_seq(void);
extern int get_mid_seq(void);

int g_main_seq = 0;

__attribute__((constructor))
void main_ctor(void) {
    g_main_seq = ++g_sequence;
}

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
    if (get_leaf_seq() == 1 && get_mid_seq() == 2 && g_main_seq == 3 && g_sequence == 3) {
        sys_write(1, "CTOR_CHAIN_OK: 1-2-3\n", 21);
        sys_exit(0);
    } else {
        sys_write(1, "CTOR_ERR\n", 9);
        sys_exit(1);
    }
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
