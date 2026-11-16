#include <cstdint>
#include <iostream>
#include <string>

#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

using namespace winios;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_early_physical_allocator() {
    std::cout << "[Test 1] Early Physical Allocator Alignment & Exhaustion...\n";

    Memory memory(0x400000); // 4 MB

    constexpr std::uint64_t kBase  = 0x100000ULL; // topOfKernelData floor
    constexpr std::uint64_t kLimit = 0x140000ULL; // 256 KB capacity (16 pages)

    EarlyPhysicalPageAllocator allocator(kBase, kLimit);
    TEST_CHECK(allocator.current_watermark() == kBase, "Initial watermark matches base");
    TEST_CHECK(allocator.remaining_bytes() == 0x40000ULL, "Initial remaining matches capacity");

    // 1. Single page allocation (16 KB)
    const std::uint64_t p1 = allocator.allocate_pages(memory, 1);
    TEST_CHECK(p1 == kBase, "First allocation starts at base");
    TEST_CHECK((p1 % 0x4000ULL) == 0, "Page 1 is 16KB aligned");
    TEST_CHECK(allocator.allocated_bytes() == 0x4000ULL, "Allocated 16 KB");

    // 2. Custom alignment: allocate 2 pages aligned to 64 KB (0x10000)
    const std::uint64_t p2 = allocator.allocate_pages(memory, 2, 0x10000ULL);
    TEST_CHECK(p2 == 0x110000ULL, "Page 2 aligned up to 64KB boundary");
    TEST_CHECK((p2 % 0x10000ULL) == 0, "Page 2 matches 64KB alignment");
    TEST_CHECK(allocator.current_watermark() == 0x118000ULL, "Watermark advanced to 0x118000");

    // 3. Exhaustion test: attempt allocation that exceeds remaining capacity
    // Remaining is 0x140000 - 0x118000 = 0x28000 (160 KB). Request 12 pages (192 KB).
    const std::uint64_t p_fail = allocator.allocate_pages(memory, 12);
    TEST_CHECK(p_fail == 0ULL, "Allocation exceeding capacity must fail and return 0");
    TEST_CHECK(allocator.current_watermark() == 0x118000ULL, "Failed allocation must not advance watermark");

    // 4. Exact fit allocation: request 10 pages (160 KB)
    const std::uint64_t p_fit = allocator.allocate_pages(memory, 10);
    TEST_CHECK(p_fit == 0x118000ULL, "Exact fit allocation succeeds");
    TEST_CHECK(allocator.remaining_bytes() == 0ULL, "Capacity completely exhausted");

    // 5. Subsequent allocation after full exhaustion
    const std::uint64_t p_empty = allocator.allocate_pages(memory, 1);
    TEST_CHECK(p_empty == 0ULL, "Allocation on exhausted pool must fail");

    std::cout << "  -> Early physical allocator verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.15 Early Physical Allocator Tests ===\n";
    test_early_physical_allocator();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.15 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.15 Tests FAILED ===\n";
        return 1;
    }
}
