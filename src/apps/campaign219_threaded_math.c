// WinIOS Campaign 219: Sample Application 3 - Multi-Threaded Math Workload
#include <stdint.h>

static inline void sys_exit(int code) {
    register int64_t x0 __asm__("x0") = code;
    register int64_t x16 __asm__("x16") = 1; // SYS_exit
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x16) : "memory");
    __builtin_unreachable();
}

int main(void) {
    // Computes factorial(5) + 200 = 120 + 200 = 320
    int accum = 1;
    for (int i = 1; i <= 5; ++i) {
        accum *= i;
    }
    accum += 200;
    sys_exit(accum);
    return 0;
}
