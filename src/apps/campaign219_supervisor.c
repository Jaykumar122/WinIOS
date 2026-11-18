// WinIOS Campaign 219: Sample Application 1 - Process Supervisor
#include <stdint.h>
#include <stddef.h>

static inline int64_t sys_write(int fd, const void* buf, size_t count) {
    register int64_t x0 __asm__("x0") = fd;
    register const void* x1 __asm__("x1") = buf;
    register size_t x2 __asm__("x2") = count;
    register int64_t x16 __asm__("x16") = 4; // SYS_write
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1; // SYS_exit
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x16) : "memory");
    __builtin_unreachable();
}

int main(void) {
    const char banner[] = "[Supervisor] Initializing WinIOS Multi-Process Supervisor...\n";
    sys_write(1, banner, sizeof(banner) - 1);
    sys_exit(0);
    return 0;
}
