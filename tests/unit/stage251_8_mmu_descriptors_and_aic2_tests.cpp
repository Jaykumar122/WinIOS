// ============================================================================
// Stage 251.5 - 251.8: MMU Descriptor Audit, Physical Page Metadata,
// XnuVmMap Validation & AIC2 Platform Truth Tests
// ============================================================================

#include "cpu/cpu.h"
#include "device/apple_aic.h"
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
using namespace winios::device;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 251.5: Page Table Descriptor-by-Descriptor Audit
// ----------------------------------------------------------------------------
static void test_stage251_5_mmu_descriptor_audit() {
    std::cout << "[Stage 251.5] Testing Page Table Descriptor-by-Descriptor Audit...\n";

    CPU cpu;
    Memory memory(0x800000); // 8 MiB RAM
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // Execute through arm_vm_init_early
    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    const uint64_t pt_base = memory.read_u64(0x10010ULL); // l1_page_table_pa
    TEST_ASSERT(pt_base >= 0x20000ULL, "Page table base is in stolen memory");

    // 1. Decode Entry 0: DRAM base block descriptor
    const uint64_t desc0 = memory.read_u64(pt_base + 0);
    const uint32_t type0 = desc0 & 0x3;
    TEST_ASSERT(type0 == 0x1, "Entry 0 is valid Block descriptor (type 0b01)");
    const uint32_t attr0 = (desc0 >> 2) & 0x7;
    TEST_ASSERT(attr0 == 0, "Entry 0 AttrIndx is 0 (Normal Cacheable)");
    const uint32_t ap0 = (desc0 >> 6) & 0x3;
    TEST_ASSERT(ap0 == 0, "Entry 0 AP[2:1] is 0b00 (Kernel Read/Write, no user)");
    const uint32_t sh0 = (desc0 >> 8) & 0x3;
    TEST_ASSERT(sh0 == 3, "Entry 0 SH[1:0] is 0b11 (Inner Shareable)");
    const uint32_t af0 = (desc0 >> 10) & 0x1;
    TEST_ASSERT(af0 == 1, "Entry 0 Access Flag (AF) is set");

    // 2. Decode Entry 1: Invalidated ghost RAM boundary
    const uint64_t desc1 = memory.read_u64(pt_base + 8);
    TEST_ASSERT(desc1 == 0x0ULL, "Entry 1 is strictly 0x0 (invalid, preventing over-mapping)");

    // 3. Decode Entry 285: AIC MMIO aperture
    const uint64_t aic_desc = memory.read_u64(pt_base + (285 * 8));
    TEST_ASSERT((aic_desc & 0x3) == 0x1, "AIC descriptor is valid block entry");
    TEST_ASSERT(((aic_desc >> 2) & 0x7) == 1, "AIC AttrIndx is 1 (Device-nGnRE)");
    const uint64_t aic_output_addr = aic_desc & 0x0000FFFFFE000000ULL;
    TEST_ASSERT(aic_output_addr == 0x23A000000ULL, "AIC block output address covers 0x23B100000 at offset 0x1100000");

    std::cout << "[Stage 251.5] Descriptor-by-descriptor audit verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 251.6: Physical Page Accounting & Lifecycle
// ----------------------------------------------------------------------------
static void test_stage251_6_page_accounting_lifecycle() {
    std::cout << "[Stage 251.6] Testing Physical Page Accounting & Lifecycle...\n";

    CPU cpu;
    Memory memory(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    KernelBootCoordinator::prepare_and_boot(cpu, memory, config);

    // Step through pmap_init and vm_page_bootstrap
    for (std::size_t step = 0; step < 5000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    // 1. Audit Header at 0x28000
    const uint32_t magic = memory.read_u32(0x28000ULL);
    TEST_ASSERT(magic == 0x584E5550u, "Page metadata header magic 'XNUP'");
    const uint32_t total = memory.read_u32(0x28004ULL);
    TEST_ASSERT(total == 512, "Total frames = 512 (8 MiB / 16 KB)");
    const uint32_t managed = memory.read_u32(0x28008ULL);
    TEST_ASSERT(managed == 496, "Managed frames = 496 (512 - 16 reserved)");
    const uint32_t free_pages = memory.read_u32(0x2800CULL);
    TEST_ASSERT(free_pages == 496, "Free pages initially = 496");

    // 2. Audit Attribute Array at 0x28020
    for (uint32_t f = 0; f < 16; ++f) {
        TEST_ASSERT(memory.read_u8(0x28020ULL + f) == 0x00, "Reserved frame marked unmanaged");
    }
    for (uint32_t f = 16; f < 512; ++f) {
        TEST_ASSERT(memory.read_u8(0x28020ULL + f) == 0x04, "Frame marked XNU_PHYS_MANAGED (0x04)");
    }

    // 3. Test page descriptor pool at 0x28220
    const uint32_t first_phys_page = memory.read_u32(0x28220ULL);
    TEST_ASSERT(first_phys_page == 16, "First managed page descriptor starts at Frame 16 (0x40000)");

    std::cout << "[Stage 251.6] Physical page accounting verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 251.7: Kernel Virtual Memory Map Validation
// ----------------------------------------------------------------------------
static void test_stage251_7_xnu_vm_map_validation() {
    std::cout << "[Stage 251.7] Testing Kernel Virtual Memory Map Validation...\n";

    XnuVmMap vmap(0x0ULL, 0x800000ULL, "kernel_map");

    // Verify fields and classifications
    TEST_ASSERT(vmap.name() == "kernel_map", "Map name matches kernel_map");
    TEST_ASSERT(vmap.min_offset() == 0x0ULL, "min_offset is 0x0 (Upstream-compatible)");
    TEST_ASSERT(vmap.max_offset() == 0x800000ULL, "max_offset is 8 MiB (Upstream-compatible)");

    // Mapping insertion and lookup
    std::string err;
    TEST_ASSERT(vmap.vm_map_enter(0x10000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, kVmProtRead | kVmProtExecute, "text", &err), "Enter text");
    TEST_ASSERT(vmap.vm_map_enter(0x20000ULL, 0x8000ULL, kVmProtRead | kVmProtWrite, kVmProtRead | kVmProtWrite, "data", &err), "Enter data");

    const auto* entry = vmap.vm_map_lookup(0x10100ULL);
    TEST_ASSERT(entry != nullptr, "Lookup 0x10100 finds entry");
    TEST_ASSERT(entry->tag == "text", "Tag matches 'text'");

    // Range removal
    TEST_ASSERT(vmap.vm_map_remove(0x20000ULL, 0x8000ULL, &err), "Remove data entry");
    TEST_ASSERT(vmap.vm_map_lookup(0x20100ULL) == nullptr, "Entry no longer present after removal");

    std::cout << "[Stage 251.7] Kernel virtual memory map validation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 251.8: AIC2 vs GICv3 Platform Truth
// ----------------------------------------------------------------------------
static void test_stage251_8_aic2_platform_truth() {
    std::cout << "[Stage 251.8] Testing AIC2 vs GICv3 Platform Truth...\n";

    // Instantiate AppleAIC
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    // 1. Query AIC_INFO register
    const uint32_t info = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegInfo, 4));
    TEST_ASSERT(info == 0x06010200u, "AIC_INFO returns 0x06010200 (AIC2, 6 cores)");

    // 2. Query AIC_WHOAMI on core 0
    const uint32_t whoami = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffWhoAmI, 4));
    TEST_ASSERT(whoami == 0, "AIC_WHOAMI returns primary core index 0");

    // 3. Configure and trigger interrupt line (e.g. Timer IRQ 16)
    aic.write_register(AppleAIC::kRegTargetBase + (16 * 4), 0x1, 4); // Route to core 0
    aic.unmask_hw_irq(16);
    aic.set_hw_irq(16, true);

    TEST_ASSERT(aic.has_pending_interrupt(0), "Interrupt 16 is pending on core 0");

    // 4. Acknowledge interrupt via EVENT register
    const uint32_t event = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4));
    TEST_ASSERT((event & 0xFFFF) == 16, "AIC_EVENT returned pending IRQ 16");

    // 5. Complete interrupt via EOI write to Event register
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, event, 4);
    aic.set_hw_irq(16, false);
    TEST_ASSERT(!aic.has_pending_interrupt(0), "Interrupt 16 deasserted and acknowledged");

    std::cout << "[Stage 251.8] AIC2 platform truth and lifecycle verified.\n";
}

int main() {
    std::cout << "============================================================\n" << std::flush;
    std::cout << "Running Stage 251.5 - 251.8 MMU Descriptors & AIC2 Tests\n" << std::flush;
    std::cout << "============================================================\n" << std::flush;

    try {
        test_stage251_5_mmu_descriptor_audit();
        test_stage251_6_page_accounting_lifecycle();
        test_stage251_7_xnu_vm_map_validation();
        test_stage251_8_aic2_platform_truth();
    } catch (const std::exception& e) {
        std::cerr << "Exception caught in main: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught in main\n" << std::endl;
        return 1;
    }

    std::cout << "\n>>> ALL STAGE 251.5 - 251.8 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
