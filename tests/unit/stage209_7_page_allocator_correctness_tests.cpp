#include <iostream>
#include <cstdint>
#include <vector>
#include <string>

#include "memory/xnu_page_allocator.h"

using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_page_allocator_correctness() {
    std::cout << "[Test 1] Stage 209.7: Page Allocator Correctness, Exhaustion & Double-Free Tests...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_CHECK(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Allocator initialized with 8 MiB RAM");

    // Reserve stolen boot/table range [0x0, 0x40000) (16 pages)
    TEST_CHECK(alloc.reserve_range(0x0ULL, 0x40000ULL, "reserved_boot", &err), "Reserved first 16 pages");
    TEST_CHECK(alloc.free_pages_count() == 496, "Exactly 496 free pages initially");

    // 1. Single page allocation
    auto p1 = alloc.allocate_pages(1);
    TEST_CHECK(p1.has_value(), "Allocated single page");
    TEST_CHECK(p1.value() == 0x40000ULL, "First allocated page is 0x40000 (PFN 16)");
    TEST_CHECK(alloc.free_pages_count() == 495, "Free count decremented to 495");

    // 2. Multi-page contiguous allocation (4 pages = 64KB)
    auto p2 = alloc.allocate_pages(4);
    TEST_CHECK(p2.has_value(), "Allocated 4 contiguous pages");
    TEST_CHECK(p2.value() == 0x44000ULL, "Contiguous block allocated at 0x44000");
    TEST_CHECK(alloc.free_pages_count() == 491, "Free count decremented to 491");

    // 3. Free single page and reallocate (deterministic first-fit)
    TEST_CHECK(alloc.free_pages(p1.value(), 1, &err), "Freed page at 0x40000");
    TEST_CHECK(alloc.free_pages_count() == 492, "Free count incremented to 492");

    auto p3 = alloc.allocate_pages(1);
    TEST_CHECK(p3.has_value() && p3.value() == 0x40000ULL, "Reallocated freed page at 0x40000 (deterministic order)");

    // 4. Double-free rejection
    TEST_CHECK(alloc.free_pages(p3.value(), 1, &err), "Freed page at 0x40000");
    TEST_CHECK(!alloc.free_pages(p3.value(), 1, &err), "Second free on 0x40000 rejected");
    TEST_CHECK(!err.empty(), "Error message returned for double-free attempt");

    // 5. Allocator Exhaustion
    std::vector<std::uint64_t> bulk;
    while (alloc.free_pages_count() > 0) {
        auto p = alloc.allocate_pages(1);
        if (!p.has_value()) break;
        bulk.push_back(p.value());
    }

    TEST_CHECK(alloc.free_pages_count() == 0, "All 496 allocatable pages exhausted");
    auto p_overflow = alloc.allocate_pages(1);
    TEST_CHECK(!p_overflow.has_value(), "Allocation correctly fails when pool is fully exhausted");

    // 6. Free all allocated pages
    for (auto addr : bulk) {
        alloc.free_pages(addr, 1);
    }
    alloc.free_pages(p2.value(), 4);
    alloc.free_pages(p3.value(), 1);

    TEST_CHECK(alloc.free_pages_count() == 496, "All 496 pages successfully reclaimed");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.7 Page Allocator Correctness Tests ===\n";
    test_page_allocator_correctness();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.7: All page allocator correctness tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.7: " << g_failures << " tests failed.\n";
        return 1;
    }
}
