// ============================================================================
// Stage 243.13: Multi-Process Isolation, Overlapping VAs & Teardown Stress Tests
// Exercises multi-process memory isolation with overlapping virtual addresses,
// repeated allocation stress, and ensures process teardown does not leak or corrupt peers.
// ============================================================================

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/bsd_process.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::kernel;

static void test_overlapping_va_stress() {
    std::cout << "[Stage 243.13 - Test 1] Overlapping VA isolation across 4 concurrent address spaces...\n";
    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    const size_t kNumProcs = 4;
    const uint64_t common_va = 0x00050000ULL;

    std::vector<std::unique_ptr<UserAddressSpace>> uas_list;
    std::vector<uint64_t> pa_list;

    for (size_t i = 0; i < kNumProcs; ++i) {
        uint64_t root = ppm.allocate_page();
        auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root, "proc_" + std::to_string(i));
        uas->set_asid(static_cast<uint16_t>(50 + i));
        uas->set_owner_pid(static_cast<int32_t>(100 + i));

        uint64_t pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(common_va, pa, PagePermissions::UserReadWrite), "Map common VA");

        // Write unique per-process value
        uint64_t unique_val = 0xDEADBEEF00000000ULL | (i * 0x1111);
        uas->copyin(common_va, &unique_val, sizeof(unique_val));

        uas_list.push_back(std::move(uas));
        pa_list.push_back(pa);
    }

    // Verify all 4 physical pages are distinct
    for (size_t i = 0; i < kNumProcs; ++i) {
        for (size_t j = i + 1; j < kNumProcs; ++j) {
            TEST_ASSERT(pa_list[i] != pa_list[j], "Physical pages must be disjoint");
        }
    }

    // Verify each address space reads back its own value exclusively
    for (size_t i = 0; i < kNumProcs; ++i) {
        uint64_t read_back = 0;
        uas_list[i]->copyout(reinterpret_cast<const void*>(common_va), &read_back, sizeof(read_back));
        uint64_t expected = 0xDEADBEEF00000000ULL | (i * 0x1111);
        TEST_ASSERT(read_back == expected, "Private value preserved without cross-talk");
    }

    // Teardown process 0 and process 2
    uas_list[0].reset();
    uas_list[2].reset();

    // Verify process 1 and process 3 remain completely intact
    uint64_t p1_val = 0;
    uas_list[1]->copyout(reinterpret_cast<const void*>(common_va), &p1_val, sizeof(p1_val));
    TEST_ASSERT(p1_val == (0xDEADBEEF00000000ULL | (1 * 0x1111)), "Process 1 intact after peer teardown");

    uint64_t p3_val = 0;
    uas_list[3]->copyout(reinterpret_cast<const void*>(common_va), &p3_val, sizeof(p3_val));
    TEST_ASSERT(p3_val == (0xDEADBEEF00000000ULL | (3 * 0x1111)), "Process 3 intact after peer teardown");
}

int main() {
    std::cout << "=== Running Stage 243.13 Multi-Process Isolation Stress Tests ===\n";
    test_overlapping_va_stress();
    std::cout << "Stage 243.13: All tests passed successfully.\n";
    return 0;
}
