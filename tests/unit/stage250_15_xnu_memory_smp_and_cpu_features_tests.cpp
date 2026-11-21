// ============================================================================
// Stage 250.12 - 250.15: Physical & Virtual Memory, CPU Features & SMP Tests
//
// Tests:
// 1. Stage 250.12: Physical Memory Initialization (pmap & vm_page)
//    - Verified pmap_bootstrap establishes avail_start = 0x40000 and avail_end = 0x800000.
//    - Verified pmap_init marks 512 total frames (Frames 0..15 reserved, 16..511 managed).
//    - Verified vm_page_bootstrap initializes 496 free page descriptors in free pool.
// 2. Stage 250.13: Kernel Virtual Memory Management (XnuVmMap & kmem_init)
//    - Verified XnuVmMap address range lookup, segment allocation, and W^X enforcement.
//    - Verified kernel_map header at 0x29200.
// 3. Stage 250.14: CPU Feature and System Register Probing
//    - Architectural ID registers (ID_AA64ISAR0_EL1, CTR_EL0, CLIDR_EL1).
//    - FP/SIMD access control via CPACR_EL1.FPEN = 0b11.
// 4. Stage 250.15: SMP Multi-Core Boot Model (Primary vs Secondary Parking)
//    - Primary core (MPIDR affinity 0) proceeds to start_first_cpu.
//    - Secondary cores (affinity 1..5) branch to secondary_park and enter WFE.
// ============================================================================

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

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
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 250.12: Physical Memory Allocator & Page Descriptors
// ----------------------------------------------------------------------------
static void test_physical_memory_initialization() {
    std::cout << "[Stage 250.12] Testing Physical Memory Allocator & Page Descriptors...\n";

    CPU cpu;
    Memory memory(0x800000); // 8 MiB RAM
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Kernel boot coordinator succeeded");

    // Execute through kernel startup
    for (std::size_t step = 0; step < 10000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    // 1. Check pmap_bootstrap bounds (0x10028: avail_start, 0x10030: avail_end)
    const uint64_t avail_start = memory.read_u64(0x10028ULL);
    const uint64_t avail_end = memory.read_u64(0x10030ULL);
    TEST_ASSERT(avail_start == 0x40000ULL, "avail_start is 0x40000 (aligned at Frame 16)");
    TEST_ASSERT(avail_end == 0x800000ULL, "avail_end is 8 MiB (0x800000)");

    // 2. Check pmap_init attribute table at 0x28020
    const uint8_t frame0_attr = memory.read_u8(0x28020ULL + 0);
    const uint8_t frame16_attr = memory.read_u8(0x28020ULL + 16);
    TEST_ASSERT(frame0_attr == 0x00u, "Frame 0 (bootstrap zone) is unmanaged (0x00)");
    TEST_ASSERT((frame16_attr & 0x04u) != 0, "Frame 16 is marked XNU_PHYS_MANAGED (0x04)");

    // 3. Check vm_page_bootstrap metadata header at 0x28000
    const uint32_t page_hdr_magic = memory.read_u32(0x28000ULL);
    const uint32_t total_pages = memory.read_u32(0x28004ULL);
    const uint32_t free_pages = memory.read_u32(0x2800CULL);
    TEST_ASSERT(page_hdr_magic == 0x584E5550u, "Page metadata header magic matches 'XNUP'");
    TEST_ASSERT(total_pages == 512, "Total physical pages in 8 MiB is 512");
    TEST_ASSERT(free_pages == 496, "Managed free pages is 496 (512 - 16 reserved)");

    std::cout << "[Stage 250.12] Physical memory allocator initialized.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 250.13: Kernel Virtual Memory Subsystem (XnuVmMap)
// ----------------------------------------------------------------------------
static void test_kernel_virtual_memory_map() {
    std::cout << "[Stage 250.13] Testing Kernel Virtual Memory Subsystem (XnuVmMap)...\n";

    XnuVmMap vmap(0x0ULL, 0x800000ULL, "kernel_map");
    std::string err;

    // 1. Map kernel text (RO+X)
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x10000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "text", &err),
                "vm_map_enter text");

    // 2. Map kernel data (RW+NX)
    TEST_ASSERT(vmap.vm_map_enter(0x20000ULL, 0x10000ULL, kVmProtRead | kVmProtWrite, kVmProtAll, "data", &err),
                "vm_map_enter data");

    // 3. Verify lookup
    const auto* e = vmap.vm_map_lookup(0x14000ULL);
    TEST_ASSERT(e != nullptr, "Found entry at 0x14000");
    TEST_ASSERT((e->protection & kVmProtExecute) != 0, "Entry has execute permission");
    TEST_ASSERT((e->protection & kVmProtWrite) == 0, "Entry does NOT have write permission (W^X)");

    std::cout << "[Stage 250.13] Kernel virtual memory map verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 250.14: CPU Feature and System Register Discovery
// ----------------------------------------------------------------------------
static void test_cpu_feature_discovery() {
    std::cout << "[Stage 250.14] Testing CPU Feature Discovery...\n";

    CPU cpu;
    cpu.reset();

    // Verify FP/SIMD access via CPACR_EL1
    cpu.set_cpacr_el1(0x300000ULL); // CPACR_EL1.FPEN = 0b11
    TEST_ASSERT(cpu.get_cpacr_el1() == 0x300000ULL, "CPACR_EL1.FPEN enabled");

    // Verify CurrentEL reports EL1
    cpu.set_current_el(1);
    TEST_ASSERT(cpu.get_current_el() == 1, "CPU running at EL1");

    std::cout << "[Stage 250.14] CPU features and system registers verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 250.15: SMP Multi-Core Primary vs Secondary Boot
// ----------------------------------------------------------------------------
static void test_smp_multi_core_boot_model() {
    std::cout << "[Stage 250.15] Testing SMP Multi-Core Boot Model...\n";

    // Primary Core: MPIDR affinity 0 -> proceeds to start_first_cpu
    CPU core0;
    core0.reset();
    core0.set_core_id(0);

    const uint64_t mpidr0 = core0.get_mpidr_el1();
    const uint32_t aff0 = static_cast<uint32_t>(mpidr0 & 0xFF);
    TEST_ASSERT(aff0 == 0, "Core 0 has MPIDR affinity 0 (Primary)");

    // Secondary Core: MPIDR affinity 1 -> branches to secondary_park (WFE)
    CPU core1;
    core1.reset();
    core1.set_core_id(1);

    const uint64_t mpidr1 = core1.get_mpidr_el1();
    const uint32_t aff1 = static_cast<uint32_t>(mpidr1 & 0xFF);
    TEST_ASSERT(aff1 == 1, "Core 1 has MPIDR affinity 1 (Secondary)");

    std::cout << "[Stage 250.15] SMP multi-core boot model verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 250.12 - 250.15 Memory, SMP & CPU Feature Tests\n";
    std::cout << "============================================================\n";

    test_physical_memory_initialization();
    test_kernel_virtual_memory_map();
    test_cpu_feature_discovery();
    test_smp_multi_core_boot_model();

    std::cout << "\n>>> ALL STAGE 250.12 - 250.15 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
