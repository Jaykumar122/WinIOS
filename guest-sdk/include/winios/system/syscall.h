#ifndef WINIOS_GUEST_SDK_SYSCALL_H
#define WINIOS_GUEST_SDK_SYSCALL_H

#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__)

static inline int64_t winios_syscall0(uint64_t num) {
    register int64_t x0 __asm__("x0");
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "=r"(x0)
        : "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall1(uint64_t num, uint64_t a0) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x1), "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall5(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    register uint64_t x4 __asm__("x4") = a4;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
        : "memory"
    );
    return x0;
}

static inline int64_t winios_syscall6(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    register int64_t x0 __asm__("x0") = (int64_t)a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    register uint64_t x4 __asm__("x4") = a4;
    register uint64_t x5 __asm__("x5") = a5;
    register uint64_t x16 __asm__("x16") = num;
    __asm__ volatile(
        "svc #0x80\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
        : "memory"
    );
    return x0;
}

#else

// Host stub for compile-time syntax check on non-ARM64 hosts
static inline int64_t winios_syscall0(uint64_t num) { (void)num; return -1; }
static inline int64_t winios_syscall1(uint64_t num, uint64_t a0) { (void)num; (void)a0; return -1; }
static inline int64_t winios_syscall2(uint64_t num, uint64_t a0, uint64_t a1) { (void)num; (void)a0; (void)a1; return -1; }
static inline int64_t winios_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) { (void)num; (void)a0; (void)a1; (void)a2; return -1; }
static inline int64_t winios_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) { (void)num; (void)a0; (void)a1; (void)a2; (void)a3; return -1; }
static inline int64_t winios_syscall5(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) { (void)num; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; return -1; }
static inline int64_t winios_syscall6(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) { (void)num; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; return -1; }

#endif

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_SYSCALL_H