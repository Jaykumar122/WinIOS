#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "runtime/guest_allocator.h"
#include "runtime/crt/mini_crt.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

void test_guest_allocator_basic() {
    GuestAllocator alloc(0x200000000ULL, 1024 * 1024, 512 * 1024, 42);

    uint64_t ptr1 = alloc.Allocate(64, 16);
    TEST_ASSERT(ptr1 != 0);
    TEST_ASSERT((ptr1 & 0xF) == 0); // 16-byte aligned
    TEST_ASSERT(alloc.IsValidPointer(ptr1));
    TEST_ASSERT(alloc.GetAllocationSize(ptr1) == 64);

    uint64_t ptr2 = alloc.Allocate(128, 16);
    TEST_ASSERT(ptr2 != 0);
    TEST_ASSERT(ptr2 > ptr1);

    auto stats = alloc.GetStats();
    TEST_ASSERT(stats.active_allocations_count == 2);
    TEST_ASSERT(stats.bytes_allocated == 64 + 128);

    // Free ptr1
    TEST_ASSERT(alloc.Free(ptr1));
    TEST_ASSERT(!alloc.IsValidPointer(ptr1));
    TEST_ASSERT(!alloc.Free(ptr1)); // Double free rejected

    stats = alloc.GetStats();
    TEST_ASSERT(stats.active_allocations_count == 1);
    TEST_ASSERT(stats.bytes_allocated == 128);

    std::cout << "[PASS] test_guest_allocator_basic\n";
}

void test_allocator_quota_and_realloc() {
    // 1000 byte quota
    GuestAllocator alloc(0x200000000ULL, 1024 * 1024, 1000, 42);

    uint64_t p1 = alloc.Allocate(600);
    TEST_ASSERT(p1 != 0);

    // Exceeds quota (600 + 500 = 1100 > 1000)
    uint64_t p2 = alloc.Allocate(500);
    TEST_ASSERT(p2 == 0);

    // Reallocate within quota
    uint64_t p3 = alloc.Reallocate(p1, 800);
    TEST_ASSERT(p3 != 0);

    // Reallocate beyond quota
    uint64_t p4 = alloc.Reallocate(p3, 1200);
    TEST_ASSERT(p4 == 0);

    std::vector<AllocationRecord> leaks;
    size_t leak_count = alloc.DetectLeaks(&leaks);
    TEST_ASSERT(leak_count == 1);
    TEST_ASSERT(leaks[0].guest_address == p3);

    alloc.Reset();
    TEST_ASSERT(alloc.DetectLeaks() == 0);

    std::cout << "[PASS] test_allocator_quota_and_realloc\n";
}

void test_mini_crt() {
    char buf[64];
    w_memset(buf, 0, sizeof(buf));
    w_strcpy(buf, "Hello");
    TEST_ASSERT(w_strlen(buf) == 5);
    TEST_ASSERT(w_strcmp(buf, "Hello") == 0);
    TEST_ASSERT(w_strncmp(buf, "Help", 3) == 0);

    w_strcat(buf, " WinIOS");
    TEST_ASSERT(w_strcmp(buf, "Hello WinIOS") == 0);

    char num_buf[32];
    w_itoa(12345, num_buf, 10);
    TEST_ASSERT(w_strcmp(num_buf, "12345") == 0);
    TEST_ASSERT(w_atoi("12345") == 12345);
    TEST_ASSERT(w_atoi("-42") == -42);

    char fmt_buf[64];
    w_snprintf(fmt_buf, sizeof(fmt_buf), "Val=%d, Str=%s", 99, "Test");
    TEST_ASSERT(w_strcmp(fmt_buf, "Val=99, Str=Test") == 0);

    std::cout << "[PASS] test_mini_crt\n";
}

int main() {
    test_guest_allocator_basic();
    test_allocator_quota_and_realloc();
    test_mini_crt();
    std::cout << "All Stage 217.5-217.6 allocator and mini CRT tests passed.\n";
    return 0;
}
