#include <iostream>
#include <cstdint>
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

void test_physical_page_allocator() {
    std::cout << "[Test 1] Stage 208.10: Physical Page Allocator (16KB Granule)...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;

    // 1. Initialize with 4 MB RAM at 0x0
    const bool init_ok = alloc.initialize(0x0ULL, 0x400000ULL, &err);
    TEST_CHECK(init_ok, "Allocator initialized with 4MB RAM");
    TEST_CHECK(alloc.total_pages() == 256, "Total pages = 256 (4MB / 16KB)");
    TEST_CHECK(alloc.free_pages_count() == 256, "All 256 pages initially free");

    // 2. Reserve critical boot regions
    // Stack: 0x0 - 0x10000 (4 pages)
    TEST_CHECK(alloc.reserve_range(0x0, 0x10000, "bootstrap_stack", &err), "Reserved bootstrap stack");
    // Globals: 0x10000 - 0x20000 (4 pages)
    TEST_CHECK(alloc.reserve_range(0x10000, 0x10000, "globals", &err), "Reserved globals");
    // Kernel Mach-O image: 0x20000 - 0x30000 (4 pages)
    TEST_CHECK(alloc.reserve_range(0x20000, 0x10000, "kernel_text", &err), "Reserved kernel text");
    // Stolen page table root: 0x30000 - 0x40000 (4 pages)
    TEST_CHECK(alloc.reserve_range(0x30000, 0x10000, "page_tables", &err), "Reserved page tables");

    TEST_CHECK(alloc.reserved_pages_count() == 16, "16 pages reserved (256KB)");
    TEST_CHECK(alloc.free_pages_count() == 240, "240 pages remaining free");

    // 3. Allocate a 16KB page frame
    auto page1 = alloc.allocate_pages(1);
    TEST_CHECK(page1.has_value(), "Allocated page 1");
    std::cout << "  -> Allocated Page 1 PA: 0x" << std::hex << page1.value() << std::dec << "\n";
    TEST_CHECK(page1.value() == 0x40000ULL, "Allocated at first free page boundary (0x40000)");
    TEST_CHECK((page1.value() & 0x3FFFULL) == 0, "Page 1 is 16KB aligned");

    // 4. Allocate contiguous multi-page block (4 pages = 64KB)
    auto block4 = alloc.allocate_pages(4);
    TEST_CHECK(block4.has_value(), "Allocated 4 contiguous pages");
    TEST_CHECK(block4.value() == 0x44000ULL, "Block 4 starts at 0x44000");

    // 5. Test Free
    TEST_CHECK(alloc.free_pages(page1.value(), 1, &err), "Freed page 1");
    TEST_CHECK(!alloc.free_pages(page1.value(), 1, &err), "Double free correctly rejected");

    // 6. Test Freeing Reserved Region (Must be rejected!)
    TEST_CHECK(!alloc.free_pages(0x0ULL, 1, &err), "Freeing reserved stack page rejected");

    // 7. Test Snapshot Serialization & Restoration
    const auto snap = alloc.serialize();
    TEST_CHECK(!snap.empty(), "Serialized allocator snapshot");

    XnuPhysicalPageAllocator alloc2;
    TEST_CHECK(alloc2.deserialize(snap.data(), snap.size(), &err), "Restored snapshot into alloc2");
    TEST_CHECK(alloc2.total_pages() == alloc.total_pages(), "Restored total pages matches");
    TEST_CHECK(alloc2.free_pages_count() == alloc.free_pages_count(), "Restored free count matches");
    TEST_CHECK(alloc2.allocated_pages_count() == alloc.allocated_pages_count(), "Restored allocated count matches");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.10 Physical Page Allocator Tests ===\n";
    test_physical_page_allocator();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.10: All physical page allocator tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.10: " << g_failures << " tests failed.\n";
        return 1;
    }
}
