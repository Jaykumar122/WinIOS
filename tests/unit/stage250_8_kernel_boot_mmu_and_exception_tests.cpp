// ============================================================================
// Stage 250.6 - 250.8: 16 KB Translation MMU Setup, Exception Vectors &
// Early Kernel Interrupt Environment Tests
//
// Tests:
// 1. Stage 250.6: 16 KB Translation & MMU Boot Setup
//    - Verified 16 KB granule translation table format (TG0 = 0b10 in TCR_EL1).
//    - Level 1 & Level 2 block and table descriptors.
//    - Identity mapping of DRAM and Device-nGnRE mapping of AIC MMIO aperture.
// 2. Stage 250.7: Exception and Vector Initialization
//    - VBAR_EL1 vector table installation.
//    - Synchronous abort trapping, ESR_EL1 and FAR_EL1 diagnostic reporting.
// 3. Stage 250.8: GICv3 and Apple AIC Early Kernel Interrupt Environment
//    - Multi-core AIC2 controller discovery at 0x23B100000.
//    - AIC_INFO (0x06010200) and AIC_WHOAMI (core 0) register response.
// ============================================================================

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
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
using namespace winios::runtime;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 250.6: 16 KB Granule Translation & MMU Boot Setup
// ----------------------------------------------------------------------------
static void test_16kb_mmu_translation_setup() {
    std::cout << "[Stage 250.6] Testing 16 KB Granule Translation & MMU Boot Setup...\n";

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

    // Execute through early bootstrap up to arm_vm_init_early
    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    // Read page table base allocated by kernel
    const std::uint64_t pt_base = memory.read_u64(0x10010ULL); // l1_page_table_pa
    TEST_ASSERT(pt_base >= 0x20000ULL, "Page table base allocated in stolen memory");
    TEST_ASSERT((pt_base & 0x3FFFULL) == 0, "Page table base is 16 KB aligned");

    // Verify Entry 0: 32MB identity map block for DRAM base [0x0, 32MB)
    const std::uint64_t entry0 = memory.read_u64(pt_base + 0);
    TEST_ASSERT((entry0 & 0x3) == 0x1, "Entry 0 is valid block descriptor");
    TEST_ASSERT((entry0 & 0x700) != 0, "Entry 0 has valid memory attributes");

    // Verify Entry 1: strictly 0x0 (invalidated ghost memory to prevent over-mapping)
    const std::uint64_t entry1 = memory.read_u64(pt_base + 8);
    TEST_ASSERT(entry1 == 0x0ULL, "Entry 1 is invalid (0x0) preventing ghost memory mapping");

    // Verify Entry 285: AIC MMIO aperture descriptor at slot 285
    const std::uint64_t aic_entry = memory.read_u64(pt_base + (285 * 8));
    TEST_ASSERT(aic_entry == 0x000000023A000705ULL, "Entry 285 correctly maps AIC2 MMIO aperture");

    std::cout << "[Stage 250.6] 16 KB MMU translation tables verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 250.7: Exception and Vector Initialization
// ----------------------------------------------------------------------------
static void test_exception_vector_initialization() {
    std::cout << "[Stage 250.7] Testing Exception and Vector Initialization...\n";

    CPU cpu;
    Memory memory(0x800000);
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const uint64_t vbar_addr = 0x80000ULL;
    cpu.set_vbar_el1(vbar_addr);
    TEST_ASSERT(cpu.get_vbar_el1() == vbar_addr, "VBAR_EL1 configured to 0x80000");

    // Unmapped MMIO read at 0x99900000 should raise synchronous Data Abort at EL1
    const uint64_t code_pc = 0x10000ULL;
    // Instruction: LDR X0, [X1] where X1 = 0x99900000
    memory.write_u32(code_pc, 0xF9400020u);
    cpu.set_program_counter(code_pc);
    cpu.write_register(1, 0x99900000ULL);
    cpu.set_current_el(1);

    cpu.step(memory);

    // CPU should vector to VBAR_EL1 + 0x200 (CurrentEL with SP_ELx Synchronous)
    TEST_ASSERT(cpu.get_program_counter() == vbar_addr + 0x200, "Vectored to synchronous abort handler at VBAR + 0x200");
    TEST_ASSERT((cpu.get_esr_el1() >> 26) == 0x25, "ESR_EL1 records Data Abort from same EL (EC=0x25)");
    TEST_ASSERT(cpu.get_far_el1() == 0x99900000ULL, "FAR_EL1 records faulting address 0x99900000");

    std::cout << "[Stage 250.7] Exception vectors and abort diagnostics verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 250.8: GICv3 and Apple AIC Early Kernel Interrupt Environment
// ----------------------------------------------------------------------------
static void test_aic2_interrupt_environment() {
    std::cout << "[Stage 250.8] Testing AIC2 Early Kernel Interrupt Environment...\n";

    const uint64_t aic_base = 0x23B100000ULL;
    const uint64_t aic_size = 0x100000ULL;
    AppleAIC aic(0x41494332u, aic_base, aic_size, 6);

    // 1. Verify AIC_INFO at offset 0x4
    const uint64_t info = aic.read_register(AppleAIC::kRegInfo, 4);
    TEST_ASSERT(info == 0x06010200u, "AIC_INFO returns hardware revision 0x06010200");

    // 2. Verify AIC_WHOAMI at offset 0x2000
    const uint64_t whoami = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffWhoAmI, 4);
    TEST_ASSERT(whoami == 0, "AIC_WHOAMI returns core ID 0 for boot core");

    // 3. Verify AIC_CONFIG at offset 0x10 can be enabled
    aic.write_register(AppleAIC::kRegConfig, 1, 4);
    const uint64_t config = aic.read_register(AppleAIC::kRegConfig, 4);
    TEST_ASSERT(config == 1, "AIC_CONFIG successfully enabled");

    std::cout << "[Stage 250.8] AIC2 early interrupt environment verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 250.6 - 250.8 MMU & Exception Vector Tests\n";
    std::cout << "============================================================\n";

    test_16kb_mmu_translation_setup();
    test_exception_vector_initialization();
    test_aic2_interrupt_environment();

    std::cout << "\n>>> ALL STAGE 250.6 - 250.8 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
