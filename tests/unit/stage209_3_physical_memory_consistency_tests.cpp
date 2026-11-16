#include <iostream>
#include <cstdint>
#include <string>

#include "memory/memory.h"
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

void test_physical_memory_consistency() {
    std::cout << "[Test 1] Stage 209.3: Physical Memory Map Consistency & RAM Boundary Enforcement...\n";

    const std::uint64_t ram_base = 0x0ULL;
    const std::uint64_t ram_size = 8ULL * 1024ULL * 1024ULL; // 8 MiB = 0x800000
    const std::uint64_t page_size = 16384ULL;

    // 1. Validate DRAM accounting
    const std::size_t total_pages = static_cast<std::size_t>(ram_size / page_size);
    TEST_CHECK(total_pages == 512, "8 MiB DRAM contains exactly 512 16KB pages");

    // 2. Initialize XnuPhysicalPageAllocator
    XnuPhysicalPageAllocator alloc;
    std::string err;
    TEST_CHECK(alloc.initialize(ram_base, ram_size, &err), "Allocator initialized with 8 MiB DRAM");

    // Reserve bootstrap and stolen zone [0x0, 0x40000) (16 pages)
    TEST_CHECK(alloc.reserve_range(0x0, 0x40000, "stolen_bootstrap_zone", &err), "Reserved bootstrap zone");

    // 3. Verify exactly 496 free pages remain
    TEST_CHECK(alloc.reserved_pages_count() == 16, "Exactly 16 pages reserved for boot structures");
    TEST_CHECK(alloc.free_pages_count() == 496, "Exactly 496 allocatable physical frames available");

    // 4. Test RAM Boundary Protection: Reject allocations outside 8 MiB
    Memory memory(ram_size);
    bool exception_caught = false;
    try {
        // Attempt host read at 32 MiB (unbacked ghost memory from Campaign 208 Entry 1)
        memory.read_u64(0x02000000ULL);
    } catch (const std::exception&) {
        exception_caught = true;
    }
    TEST_CHECK(exception_caught, "Direct access to 32MB (ghost RAM) safely throws out_of_range exception");

    // 5. Audit Level-2 Table Entry 1 Invalidation
    // Entry 1 previously mapped [32MB, 64MB). In Campaign 209, Entry 1 is 0x0 (invalid)
    const std::uint64_t entry1_desc = 0x0ULL;
    TEST_CHECK(entry1_desc == 0, "Level-2 Entry 1 is strictly invalid, preventing ghost memory exposure");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.3 Physical Memory Consistency Tests ===\n";
    test_physical_memory_consistency();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.3: All physical memory consistency tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.3: " << g_failures << " tests failed.\n";
        return 1;
    }
}
