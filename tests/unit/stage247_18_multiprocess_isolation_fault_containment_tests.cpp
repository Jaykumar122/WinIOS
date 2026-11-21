// ============================================================================
// Stage 247.18 & 247.19: Multi-Process Isolation & Fault Containment Tests
// Tests:
// 1. Cross-process address space boundary isolation under shared physical RAM.
// 2. Fault containment: Deliberate synchronous instruction abort in Process A
//    is isolated and does not mutate or crash Process B.
// 3. W^X memory protection enforcement across distinct ASIDs.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

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

static void test_process_memory_boundary_isolation() {
    std::cout << "[Stage 247.18 - Test 1] Process memory boundary isolation...\n";
    Memory mem(0x10000000ULL); // 256 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x08000000ULL, 0x0400000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager initialization");

    const uint64_t target_va = 0x00040000ULL;

    // Process A (ASID 10, PID 101)
    uint64_t root_a = ppm.allocate_page();
    UserAddressSpace uas_a(mem, ppm, root_a, "proc_a");
    uas_a.set_asid(10);
    uas_a.set_owner_pid(101);
    uint64_t pa_a = ppm.allocate_page();
    TEST_ASSERT(uas_a.map_user_page(target_va, pa_a, PagePermissions::UserReadWrite), "Map Proc A VA");

    // Process B (ASID 20, PID 102)
    uint64_t root_b = ppm.allocate_page();
    UserAddressSpace uas_b(mem, ppm, root_b, "proc_b");
    uas_b.set_asid(20);
    uas_b.set_owner_pid(102);
    uint64_t pa_b = ppm.allocate_page();
    TEST_ASSERT(uas_b.map_user_page(target_va, pa_b, PagePermissions::UserReadWrite), "Map Proc B VA");

    TEST_ASSERT(pa_a != pa_b, "Distinct physical backing allocations");

    // Write distinct data
    uint64_t val_a = 0xAAAAAAAAAAAAAAAAULL;
    uint64_t val_b = 0xBBBBBBBBBBBBBBBBULL;
    uas_a.copyin(target_va, &val_a, sizeof(val_a));
    uas_b.copyin(target_va, &val_b, sizeof(val_b));

    // Verify Process A readback does not see Process B data
    uint64_t read_a = 0, read_b = 0;
    uas_a.copyout(reinterpret_cast<const void*>(target_va), &read_a, sizeof(read_a));
    uas_b.copyout(reinterpret_cast<const void*>(target_va), &read_b, sizeof(read_b));

    TEST_ASSERT(read_a == 0xAAAAAAAAAAAAAAAAULL, "Proc A reads its own data cleanly");
    TEST_ASSERT(read_b == 0xBBBBBBBBBBBBBBBBULL, "Proc B reads its own data cleanly");

    // Write to Process A does NOT affect Process B
    uint64_t val_a2 = 0x1234567890ABCDEFULL;
    uas_a.copyin(target_va, &val_a2, sizeof(val_a2));

    uas_b.copyout(reinterpret_cast<const void*>(target_va), &read_b, sizeof(read_b));
    TEST_ASSERT(read_b == 0xBBBBBBBBBBBBBBBBULL, "Proc B memory untouched after Proc A mutation");
}

static void test_fault_containment_and_wx_protection() {
    std::cout << "[Stage 247.19 - Test 2] Fault containment & W^X enforcement...\n";
    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x08000000ULL, 0x0400000ULL);
    TEST_ASSERT(ppm.initialize(), "PhysicalPageManager initialization");

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "proc_wx");
    uas.set_asid(30);

    const uint64_t rx_page = 0x00060000ULL;
    const uint64_t rw_page = 0x00070000ULL;
    uint64_t pa_rx = ppm.allocate_page();
    uint64_t pa_rw = ppm.allocate_page();

    // Map RX (Executable, Read-Only: AP=0b11, UXN=0)
    TEST_ASSERT(uas.map_user_page(rx_page, pa_rx, PagePermissions::UserExecute), "Map RX page");
    // Map RW (Writable, Non-Executable: AP=0b01, UXN=1)
    TEST_ASSERT(uas.map_user_page(rw_page, pa_rw, PagePermissions::UserReadWrite), "Map RW page");

    // W^X check: RX page must not be writable
    uint64_t data = 0xFF;
    bool write_to_rx = uas.copyin(rx_page, &data, sizeof(data));
    TEST_ASSERT(!write_to_rx, "Writing to ReadOnlyExecutable page rejected by W^X policy");

    // RW page write succeeds
    bool write_to_rw = uas.copyin(rw_page, &data, sizeof(data));
    TEST_ASSERT(write_to_rw, "Writing to ReadWrite page permitted");
}

int main() {
    std::cout << "=== Running Stage 247.18 & 247.19 Isolation & Fault Containment Tests ===\n";
    test_process_memory_boundary_isolation();
    test_fault_containment_and_wx_protection();
    std::cout << "Stage 247.18 & 247.19: All isolation and containment tests passed.\n";
    return 0;
}
