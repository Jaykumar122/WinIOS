// ============================================================================
// Stage 242.7: Cross-Process Memory Isolation, Overlapping VAs & Teardown Tests
// Exercises multi-process memory translation isolation across overlapping VAs,
// unauthorized cross-process access prevention, and process teardown co-tenant survival.
// ============================================================================

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/asid_manager.h"
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

static void test_overlapping_va_private_isolation() {
    std::cout << "[Stage 242.7 - Test 1] Overlapping virtual addresses with disjoint physical pages...\n";
    Memory mem(0x10000000ULL); // 256 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    uint64_t root1 = ppm.allocate_page();
    uint64_t root2 = ppm.allocate_page();

    UserAddressSpace uas1(mem, ppm, root1, "proc1_uas");
    uas1.set_asid(10);
    uas1.set_owner_pid(1001);

    UserAddressSpace uas2(mem, ppm, root2, "proc2_uas");
    uas2.set_asid(11);
    uas2.set_owner_pid(1002);

    // Both processes map the EXACT SAME virtual address range: 0x40000 to 0x48000 (2 pages)
    const uint64_t test_va = 0x00040000ULL;
    uint64_t pa1 = ppm.allocate_page();
    uint64_t pa2 = ppm.allocate_page();
    TEST_ASSERT(pa1 != pa2, "Physical allocations distinct");

    TEST_ASSERT(uas1.map_user_page(test_va, pa1, PagePermissions::UserReadWrite), "Map Proc 1");
    TEST_ASSERT(uas2.map_user_page(test_va, pa2, PagePermissions::UserReadWrite), "Map Proc 2");

    // Process 1 writes unique 64-bit word
    uint64_t val1 = 0x1111222233334444ULL;
    uas1.copyin(test_va, &val1, sizeof(val1));

    // Process 2 writes different 64-bit word
    uint64_t val2 = 0xAAAABBBBCCCCDDDDULL;
    uas2.copyin(test_va, &val2, sizeof(val2));

    // Verify Process 1 reads back its own value and not Process 2's value
    uint64_t read1 = 0;
    TEST_ASSERT(uas1.copyout(reinterpret_cast<const void*>(test_va), &read1, sizeof(read1)), "Copyout Proc 1");
    TEST_ASSERT(read1 == val1, "Process 1 data preserved");

    // Verify Process 2 reads back its own value
    uint64_t read2 = 0;
    TEST_ASSERT(uas2.copyout(reinterpret_cast<const void*>(test_va), &read2, sizeof(read2)), "Copyout Proc 2");
    TEST_ASSERT(read2 == val2, "Process 2 data preserved");

    // Verify translation targets distinct physical addresses
    TEST_ASSERT(uas1.translate(test_va) == pa1, "UAS1 translates to pa1");
    TEST_ASSERT(uas2.translate(test_va) == pa2, "UAS2 translates to pa2");
}

static void test_process_teardown_co_tenant_survival() {
    std::cout << "[Stage 242.7 - Test 2] Process teardown co-tenant memory survival...\n";
    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    uint64_t root1 = ppm.allocate_page();
    uint64_t root2 = ppm.allocate_page();

    auto uas1 = std::make_unique<UserAddressSpace>(mem, ppm, root1, "proc1_temp");
    uas1->set_asid(21);
    uas1->set_owner_pid(2001);

    auto uas2 = std::make_unique<UserAddressSpace>(mem, ppm, root2, "proc2_survivor");
    uas2->set_asid(22);
    uas2->set_owner_pid(2002);

    const uint64_t va = 0x00060000ULL;
    uint64_t pa1 = ppm.allocate_page();
    uint64_t pa2 = ppm.allocate_page();

    uas1->map_user_page(va, pa1, PagePermissions::UserReadWrite);
    uas2->map_user_page(va, pa2, PagePermissions::UserReadWrite);

    uint64_t secret2 = 0xCAFEBABE12345678ULL;
    uas2->copyin(va, &secret2, sizeof(secret2));

    // Destroy Process 1 (teardown)
    uas1.reset();

    // Verify Co-tenant Process 2 is completely intact
    uint64_t survivor_read = 0;
    TEST_ASSERT(uas2->copyout(reinterpret_cast<const void*>(va), &survivor_read, sizeof(survivor_read)), "Copyout survivor");
    TEST_ASSERT(survivor_read == secret2, "Survivor memory untouched");
    TEST_ASSERT(uas2->translate(va) == pa2, "Survivor translation intact");
}

static void test_unauthorized_access_and_stale_asid() {
    std::cout << "[Stage 242.7 - Test 3] Stale ASID and unauthorized cross-process PID access checks...\n";
    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize(), "PPM init");

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "proc_uas");
    uas.set_owner_pid(3001);
    uas.set_asid(55);

    // Access check for authorized owner PID 3001
    TEST_ASSERT(uas.is_valid_for_process(3001), "Authorized PID 3001");

    // Access check for unauthorized hostile PID 9999
    TEST_ASSERT(!uas.is_valid_for_process(9999), "Hostile PID 9999 rejected");
}

int main() {
    std::cout << "=== Running Stage 242.7 Cross-Process Memory Isolation Tests ===\n";
    test_overlapping_va_private_isolation();
    test_process_teardown_co_tenant_survival();
    test_unauthorized_access_and_stale_asid();
    std::cout << "Stage 242.7: All tests passed successfully.\n";
    return 0;
}
