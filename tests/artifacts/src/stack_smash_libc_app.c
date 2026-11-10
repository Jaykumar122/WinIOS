/*
 * stack_smash_libc_app.c - Non-freestanding C program triggering stack protector
 * against libSystem.B.dylib's ___stack_chk_guard and ___stack_chk_fail.
 */
#include <stdio.h>

static volatile int g_overflow_bytes = 24;

__attribute__((noinline))
void smash_target(void) {
    volatile char buf[8];
    int limit = g_overflow_bytes;
    for (int i = 0; i < limit; ++i) {
        buf[i] = 'A';
    }
}

int main(void) {
    puts("BEFORE_SMASH");
    smash_target();
    puts("SHOULD_NOT_REACH");
    return 0;
}
