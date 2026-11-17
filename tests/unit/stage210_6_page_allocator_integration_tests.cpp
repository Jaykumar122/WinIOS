#include <iostream>
#include <vector>
#include <cstdint>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/xnu_page_allocator.h"

using namespace winios;
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

void test_xnu_physical_page_allocator() {
    std::cout << "[Test 1] Stage 210.6: XnuPhysicalPageAllocator Contiguous & Boundary Tests...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_CHECK(alloc.initialize(0x0ULL, 0x800000ULL, &err), "Allocator initialized for 8 MiB");
    TEST_CHECK(alloc.total_pages() == 512, "Total pages is 512");
    TEST_CHECK(alloc.free_pages_count() == 512, "Initial free pages is 512");

    // Reserve bootstrap and stolen zone (Frames 0..15 = 0x0 - 0x3FFFF)
    TEST_CHECK(alloc.reserve_range(0x0ULL, 0x40000ULL, "bootstrap_stolen", &err), "Reserve 16 bootstrap pages");
    TEST_CHECK(alloc.reserved_pages_count() == 16, "Reserved count is 16");
    TEST_CHECK(alloc.free_pages_count() == 496, "Free count is 496");

    // Single page allocation
    auto p1 = alloc.allocate_pages(1);
    TEST_CHECK(p1.has_value(), "Single page allocated");
    TEST_CHECK(*p1 >= 0x40000ULL, "Allocation starts at or above avail_start (0x40000)");
    TEST_CHECK((*p1 % 16384) == 0, "Allocation is 16KB aligned");
    TEST_CHECK(alloc.free_pages_count() == 495, "Free count decremented to 495");

    // Multi-page contiguous allocation (4 pages = 64KB)
    auto p4 = alloc.allocate_pages(4);
    TEST_CHECK(p4.has_value(), "4 contiguous pages allocated");
    TEST_CHECK(*p4 == *p1 + 16384, "Contiguous placement in memory");
    TEST_CHECK(alloc.free_pages_count() == 491, "Free count decremented to 491");

    // Free pages
    TEST_CHECK(alloc.free_pages(*p1, 1, &err), "Free single page");
    TEST_CHECK(alloc.free_pages_count() == 492, "Free count increased to 492");

    // Double free detection
    TEST_CHECK(!alloc.free_pages(*p1, 1, &err), "Double free properly rejected");

    // Free multi-page
    TEST_CHECK(alloc.free_pages(*p4, 4, &err), "Free 4 pages");
    TEST_CHECK(alloc.free_pages_count() == 496, "All 496 dynamic pages restored");

    // Out of memory test
    auto oom = alloc.allocate_pages(500);
    TEST_CHECK(!oom.has_value(), "Request exceeding available pages rejected");
}

void test_guest_physical_page_manager() {
    std::cout << "[Test 2] Stage 210.6: Guest PhysicalPageManager Bitmap Integration...\n";

    Memory mem(0x800000); // 8 MiB DRAM
    // Managed range: 0x40000 to 0x800000 (496 pages)
    // Bitmap in guest memory at 0x28020
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    TEST_CHECK(ppm.initialize(false), "PPM initialized");

    const auto p = ppm.allocate_page();
    TEST_CHECK(p >= 0x40000ULL, "PPM allocated page >= 0x40000");
    TEST_CHECK((p % 16384) == 0, "PPM page 16KB aligned");
    TEST_CHECK(ppm.is_page_allocated(p), "is_page_allocated returns true");

    TEST_CHECK(ppm.free_page(p), "PPM free_page succeeded");
    TEST_CHECK(!ppm.is_page_allocated(p), "is_page_allocated returns false after free");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.6 Page Allocator Integration Tests ===\n";
    test_xnu_physical_page_allocator();
    test_guest_physical_page_manager();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.6: All page allocator integration tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.6: " << g_failures << " tests failed.\n";
        return 1;
    }
}
