#include <stdint.h>

extern int mid_compute(int a, int b);

static void sys_write_all(int fd, const void* buf, uint64_t count) {
    __asm__ volatile (
        "mov x0, %0\n"
        "mov x1, %1\n"
        "mov x2, %2\n"
        "mov x16, #4\n"
        "svc #0x80\n"
        :
        : "r"((uint64_t)fd), "r"((uint64_t)buf), "r"(count)
        : "x0", "x1", "x2", "x16"
    );
}

static void sys_exit(int code) {
    __asm__ volatile (
        "mov x0, %0\n"
        "mov x16, #1\n"
        "svc #0x80\n"
        :
        : "r"((uint64_t)code)
        : "x0", "x16"
    );
}

void start(void) {
    int res = mid_compute(6, 7);
    if (res == 142) {
        sys_write_all(1, "CHAIN_OK: 142\n", 14);
        sys_exit(0);
    } else {
        sys_exit(1);
    }
}
