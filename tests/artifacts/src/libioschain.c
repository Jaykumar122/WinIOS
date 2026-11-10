/*
 * libioschain.c - Real iOS-target ARM64 dylib exporting symbols for Stage V0 Chained Fixups
 * Campaign 191-V Stage V0: compiled for aarch64-ios (PLATFORM_IOS = 2)
 */

typedef signed int int32_t;
typedef unsigned long long uint64_t;

const uint64_t g_ios_chain_bias = 5ULL;

__attribute__((noinline))
int32_t ios_chain_add(int32_t a, int32_t b) {
    return (a * 3) + b + (int32_t)g_ios_chain_bias;
}
