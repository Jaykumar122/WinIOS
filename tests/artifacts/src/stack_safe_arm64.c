#include <stdint.h>

__attribute__((noinline))
void safe_function(int len) {
    volatile char buf[8];
    for (int i = 0; i < len; ++i) {
        buf[i] = (char)(0x11 + i);
    }
}

int _start(void) {
    safe_function(4);
    __asm__ volatile (
        "mov x0, #0\n"
        "mov x16, #1\n"
        "svc #0x80\n"
    );
    return 0;
}
