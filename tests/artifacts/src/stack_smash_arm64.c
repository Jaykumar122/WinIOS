#include <stdint.h>

__attribute__((noinline))
void vulnerable_function(int overwrite_len) {
    volatile char buf[8];
    for (int i = 0; i < overwrite_len; ++i) {
        buf[i] = (char)(0xAA + i);
    }
}

int _start(void) {
    vulnerable_function(32);
    __asm__ volatile (
        "mov x0, #0\n"
        "mov x16, #1\n"
        "svc #0x80\n"
    );
    return 0;
}
