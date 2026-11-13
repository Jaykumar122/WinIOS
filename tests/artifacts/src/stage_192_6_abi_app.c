/*
 * stage_192_6_abi_app.c - Darwin Userspace libSystem ABI Conformance Application
 * Campaign 192: STAGE 192.6
 *
 * Exercises Darwin C runtime ABI contracts:
 * - Calling convention & callee-saved register preservation (x19-x28)
 * - 16-byte stack alignment
 * - Heap 16-byte alignment across varied allocation sizes
 * - Zero-size allocation semantics (malloc(0), calloc(0, 0))
 * - Allocation arithmetic overflow protection (calloc overflow)
 * - Reallocation data preservation and realloc(p, 0) == NULL semantics
 * - Double-free safety & free(NULL)
 * - Memory primitives (memcpy, memmove forward/backward overlap, memset, memcmp)
 * - String primitives (strlen, strcmp, strncmp, strcpy, strncpy)
 * - File descriptor output (write)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            const char err[] = "[FAIL] " msg "\n"; \
            write(2, err, sizeof(err) - 1); \
            exit(1); \
        } \
    } while (0)

static inline uintptr_t get_sp(void) {
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const char start_msg[] = "STAGE_192_6_ABI_START\n";
    write(1, start_msg, sizeof(start_msg) - 1);

    // 1. Stack Alignment Conformance (ARM64 requirement: SP % 16 == 0)
    uintptr_t sp = get_sp();
    CHECK((sp & 0xFULL) == 0, "Stack pointer must be 16-byte aligned");

    // 2. Callee-Saved Register Preservation Audit (x19 - x28)
    // We populate callee-saved registers with known sentinel values,
    // call libSystem routines, and assert that all sentinels are preserved.
    register uint64_t r19 __asm__("x19") = 0x1111222233334444ULL;
    register uint64_t r20 __asm__("x20") = 0x2222333344445555ULL;
    register uint64_t r21 __asm__("x21") = 0x3333444455556666ULL;
    register uint64_t r22 __asm__("x22") = 0x4444555566667777ULL;
    register uint64_t r23 __asm__("x23") = 0x5555666677778888ULL;
    register uint64_t r24 __asm__("x24") = 0x6666777788889999ULL;
    register uint64_t r25 __asm__("x25") = 0x777788889999AAAAULL;
    register uint64_t r26 __asm__("x26") = 0x88889999AAAABBBBULL;
    register uint64_t r27 __asm__("x27") = 0x9999AAAABBBBCCCCULL;
    register uint64_t r28 __asm__("x28") = 0xAAAABBBBCCCCDDDDULL;

    // Volatile function pointers to force dynamic calls through libSystem
    void* (*volatile fn_malloc)(size_t) = malloc;
    void  (*volatile fn_free)(void*) = free;
    void* (*volatile fn_calloc)(size_t, size_t) = calloc;
    void* (*volatile fn_realloc)(void*, size_t) = realloc;
    void* (*volatile fn_memcpy)(void*, const void*, size_t) = memcpy;
    void* (*volatile fn_memmove)(void*, const void*, size_t) = memmove;
    void* (*volatile fn_memset)(void*, int, size_t) = memset;
    int   (*volatile fn_memcmp)(const void*, const void*, size_t) = memcmp;
    size_t (*volatile fn_strlen)(const char*) = strlen;
    int   (*volatile fn_strcmp)(const char*, const char*) = strcmp;
    int   (*volatile fn_strncmp)(const char*, const char*, size_t) = strncmp;
    char* (*volatile fn_strcpy)(char*, const char*) = strcpy;
    char* (*volatile fn_strncpy)(char*, const char*, size_t) = strncpy;

    // 3. Heap Alignment Audit (must be 16-byte aligned for any size)
    const size_t test_sizes[] = {1, 3, 7, 15, 16, 24, 32, 63, 64, 128, 512, 1024, 40000};
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); ++i) {
        void* p = fn_malloc(test_sizes[i]);
        CHECK(p != NULL, "malloc failed");
        CHECK(((uintptr_t)p & 0xFULL) == 0, "malloc return must be 16-byte aligned");
        fn_memset(p, 0xAA, test_sizes[i]);
        fn_free(p);
    }

    // 4. Zero-Size Allocation Semantics
    void* zero_m = fn_malloc(0);
    CHECK(zero_m != NULL, "malloc(0) must return non-null valid pointer");
    CHECK(((uintptr_t)zero_m & 0xFULL) == 0, "malloc(0) must be 16-byte aligned");
    fn_free(zero_m);

    void* zero_c1 = fn_calloc(0, 0);
    CHECK(zero_c1 != NULL, "calloc(0, 0) must return non-null pointer");
    fn_free(zero_c1);

    void* zero_c2 = fn_calloc(0, 16);
    CHECK(zero_c2 != NULL, "calloc(0, 16) must return non-null pointer");
    fn_free(zero_c2);

    void* zero_c3 = fn_calloc(16, 0);
    CHECK(zero_c3 != NULL, "calloc(16, 0) must return non-null pointer");
    fn_free(zero_c3);

    // 5. Allocation Overflow Safety
    void* ovf = fn_calloc((size_t)-1, 2);
    CHECK(ovf == NULL, "calloc overflow must return NULL");

    // 6. Reallocation Semantics & Data Preservation
    void* re_init = fn_realloc(NULL, 64);
    CHECK(re_init != NULL, "realloc(NULL, size) must behave like malloc");
    CHECK(((uintptr_t)re_init & 0xFULL) == 0, "realloc pointer must be 16-byte aligned");

    // Fill with pattern
    for (int i = 0; i < 64; ++i) {
        ((uint8_t*)re_init)[i] = (uint8_t)(i + 1);
    }

    // Grow allocation
    void* re_grown = fn_realloc(re_init, 256);
    CHECK(re_grown != NULL, "realloc growth failed");
    CHECK(((uintptr_t)re_grown & 0xFULL) == 0, "grown realloc must be 16-byte aligned");
    for (int i = 0; i < 64; ++i) {
        CHECK(((uint8_t*)re_grown)[i] == (uint8_t)(i + 1), "realloc data corruption");
    }

    // Realloc with size 0 frees pointer and returns NULL
    void* re_zero = fn_realloc(re_grown, 0);
    CHECK(re_zero == NULL, "realloc(ptr, 0) must return NULL");

    // 7. Double-Free Safety & free(NULL)
    fn_free(NULL); // Must be a no-op

    void* dfree_ptr = fn_malloc(48);
    CHECK(dfree_ptr != NULL, "malloc for dfree failed");
    fn_free(dfree_ptr);
    fn_free(dfree_ptr); // Double free must not crash or corrupt allocator state

    void* next_alloc = fn_malloc(48);
    CHECK(next_alloc != NULL, "allocator must remain functional after double free");
    fn_free(next_alloc);

    // 8. Memory Primitives Conformance
    uint8_t src[32];
    uint8_t dst[32];
    for (int i = 0; i < 32; ++i) src[i] = (uint8_t)(0x10 + i);

    // memcpy
    fn_memset(dst, 0, 32);
    void* cpy_ret = fn_memcpy(dst, src, 32);
    CHECK(cpy_ret == dst, "memcpy must return destination pointer");
    CHECK(fn_memcmp(dst, src, 32) == 0, "memcpy content mismatch");

    // memmove (overlapping: dst > src)
    uint8_t overlap_buf[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    fn_memmove(overlap_buf + 2, overlap_buf, 6);
    // Elements 2..7 should now be 0, 1, 2, 3, 4, 5
    CHECK(overlap_buf[2] == 0 && overlap_buf[3] == 1 && overlap_buf[7] == 5,
          "memmove forward overlap failed");

    // memmove (overlapping: dst < src)
    fn_memmove(overlap_buf, overlap_buf + 4, 6);
    // Elements 0..5 should now be previous elements 4..9 (2, 3, 4, 5, 8, 9)
    CHECK(overlap_buf[0] == 2 && overlap_buf[1] == 3 && overlap_buf[5] == 9,
          "memmove backward overlap failed");

    // memcmp
    CHECK(fn_memcmp("abc", "abc", 3) == 0, "memcmp equal failed");
    CHECK(fn_memcmp("abc", "abd", 3) < 0, "memcmp less failed");
    CHECK(fn_memcmp("abd", "abc", 3) > 0, "memcmp greater failed");

    // 9. String Primitives Conformance
    CHECK(fn_strlen("") == 0, "strlen empty failed");
    CHECK(fn_strlen("Darwin Userspace") == 16, "strlen string failed");
    CHECK(fn_strcmp("hello", "hello") == 0, "strcmp equal failed");
    CHECK(fn_strcmp("apple", "banana") < 0, "strcmp less failed");
    CHECK(fn_strcmp("zebra", "apple") > 0, "strcmp greater failed");
    CHECK(fn_strncmp("prefix_yes", "prefix_no", 6) == 0, "strncmp equal prefix failed");
    CHECK(fn_strncmp("prefix_yes", "prefix_no", 8) != 0, "strncmp mismatch failed");

    char str_buf[32];
    fn_strcpy(str_buf, "WinIOS");
    CHECK(fn_strcmp(str_buf, "WinIOS") == 0, "strcpy failed");

    fn_memset(str_buf, 0xFF, 32);
    fn_strncpy(str_buf, "Short", 10);
    CHECK(fn_strcmp(str_buf, "Short") == 0, "strncpy content mismatch");
    // Verify remaining positions are zero-padded
    for (int k = 5; k < 10; ++k) {
        CHECK(str_buf[k] == '\0', "strncpy must zero-pad to n");
    }

    // 10. Verify Callee-Saved Registers were NOT clobbered!
    CHECK(r19 == 0x1111222233334444ULL, "x19 clobbered");
    CHECK(r20 == 0x2222333344445555ULL, "x20 clobbered");
    CHECK(r21 == 0x3333444455556666ULL, "x21 clobbered");
    CHECK(r22 == 0x4444555566667777ULL, "x22 clobbered");
    CHECK(r23 == 0x5555666677778888ULL, "x23 clobbered");
    CHECK(r24 == 0x6666777788889999ULL, "x24 clobbered");
    CHECK(r25 == 0x777788889999AAAAULL, "x25 clobbered");
    CHECK(r26 == 0x88889999AAAABBBBULL, "x26 clobbered");
    CHECK(r27 == 0x9999AAAABBBBCCCCULL, "x27 clobbered");
    CHECK(r28 == 0xAAAABBBBCCCCDDDDULL, "x28 clobbered");

    const char pass_msg[] = "STAGE_192_6_PASS\n";
    write(1, pass_msg, sizeof(pass_msg) - 1);

    return 0;
}
