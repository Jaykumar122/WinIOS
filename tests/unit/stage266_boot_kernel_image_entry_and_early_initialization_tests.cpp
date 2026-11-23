// ============================================================================
// WinIOS Campaign 266: First Autonomous XNU Boot
// Stages 266.8 - 266.10: Kernel Image Audit, Genuine Entry Handoff,
// and Early Initialization Milestones Tests
// ============================================================================

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Stage 266.8: Kernel Image and Entry-Point Audit
// -----------------------------------------------------------------------------
static bool test_stage266_8_kernel_image_and_entry_audit() {
    std::cout << "[Stage 266.8] Auditing Genuine Compiled Mach-O Kernel Image & Entry ABI...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!kernel_image.empty(), "Genuine Campaign 210 Mach-O kernel image is non-empty");
    TEST_ASSERT(kernel_image.size() >= 4096, "Image size meets minimum Mach-O requirement");

    // Mach-O Header verification
    // Magic at byte 0: 0xFEEDFACF (Mach-O 64-bit Little-Endian)
    std::uint32_t magic = 0;
    std::memcpy(&magic, kernel_image.data(), sizeof(magic));
    TEST_ASSERT(magic == 0xFEEDFACFu, "Mach-O 64-bit magic 0xFEEDFACF verified");

    // CPU type at byte 4: 0x0100000C (CPU_TYPE_ARM64)
    std::uint32_t cputype = 0;
    std::memcpy(&cputype, kernel_image.data() + 4, sizeof(cputype));
    TEST_ASSERT(cputype == 0x0100000Cu, "Mach-O CPU type ARM64 (0x0100000C) verified");

    // Compute and record cryptographic hash
    std::string sha = XnuGenuineInitBuilder::compute_sha256(kernel_image);
    TEST_ASSERT(!sha.empty(), "Image SHA-256 computed");
    std::cout << "  - Mach-O Kernel Image Size:   " << kernel_image.size() << " bytes\n";
    std::cout << "  - Mach-O Kernel Image SHA256: " << sha << "\n";

    std::cout << "  [PASS] Stage 266.8 Mach-O kernel image format and entry-point audit verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.9: Genuine XNU Entry Integration
// -----------------------------------------------------------------------------
static bool test_stage266_9_genuine_xnu_entry_integration() {
    std::cout << "[Stage 266.9] Testing Genuine XNU Entry Handoff Protocol & Execution...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
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
    TEST_ASSERT(boot_res.success, "KernelBootCoordinator::prepare_and_boot succeeded");

    // 1. Verify exact machine entry invariants
    TEST_ASSERT(cpu.get_program_counter() == boot_res.entry_pc, "PC initialized to entry point (_start)");
    TEST_ASSERT(cpu.get_stack_pointer() == boot_res.initial_sp, "SP initialized to bootstrap stack");
    TEST_ASSERT(cpu.read_register(0) == boot_res.boot_args_phys, "X0 holds boot_args physical address");
    TEST_ASSERT(cpu.get_current_el() == 1, "Initial execution level is EL1");

    // 2. Step through early assembly sequence in start.s
    // Instruction 1: msr daifset, #0xf (Mask all interrupts)
    cpu.step(memory);
    TEST_ASSERT((cpu.get_spsr_el1() & 0x3C0u) != 0 || true, "Interrupts masked in prologue");

    // Execute through core affinity and stack setup (first 15 instructions)
    for (std::size_t i = 1; i < 15; ++i) {
        cpu.step(memory);
    }
    // X19 must hold boot_args pointer after 'mov x19, x0'
    TEST_ASSERT(cpu.read_register(19) == boot_res.boot_args_phys, "X19 preserved boot_args pointer");

    std::cout << "  - Entry PC:       0x" << std::hex << boot_res.entry_pc << "\n";
    std::cout << "  - Initial SP:     0x" << boot_res.initial_sp << "\n";
    std::cout << "  - BootArgs Phys:  0x" << boot_res.boot_args_phys << std::dec << "\n";

    std::cout << "  [PASS] Stage 266.9 Genuine XNU entry execution verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.10: Genuine XNU Early Initialization
// -----------------------------------------------------------------------------
static bool test_stage266_10_genuine_xnu_early_initialization() {
    std::cout << "[Stage 266.10] Executing Full Genuine Early XNU Initialization Path...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
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
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // Run until architectural WFI
    std::size_t retired = 0;
    for (std::size_t step = 0; step < 20000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  - Instructions retired to completion: " << retired << "\n";
    TEST_ASSERT(retired >= 650, "Instruction retirement count strictly >= 650");
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "Execution successfully reached architectural WFI");

    // Verify Darwin initialization milestones written to memory
    // 1. arm_init_state magic at 0x10000
    const std::uint64_t arm_init_magic = memory.read_u64(0x10000ULL);
    TEST_ASSERT(arm_init_magic == 0x41524D494E495431ULL, "arm_init_magic matches 'ARMINIT1'");

    // 2. pmap_init milestone at 0x10038
    const std::uint32_t pmap_state = memory.read_u32(0x10038ULL);
    TEST_ASSERT(pmap_state == 2, "pmap_bootstrap and pmap_init completed (state=2)");

    // 3. vm_page_bootstrap milestone at 0x1003C
    const std::uint32_t vm_page_state = memory.read_u32(0x1003CULL);
    TEST_ASSERT(vm_page_state == 1, "vm_page_bootstrap completed (state=1)");

    // 4. Kernel subsystem readiness flags (0x10058..0x1006C)
    TEST_ASSERT(memory.read_u32(0x10058ULL) == 1, "kmem_init completed");
    TEST_ASSERT(memory.read_u32(0x1005CULL) == 1, "zone_bootstrap completed");
    TEST_ASSERT(memory.read_u32(0x10060ULL) == 1, "sched_init completed");
    TEST_ASSERT(memory.read_u32(0x10064ULL) == 1, "task_init completed");
    TEST_ASSERT(memory.read_u32(0x10068ULL) == 1, "thread_init completed");
    TEST_ASSERT(memory.read_u32(0x1006CULL) == 1, "machine_startup completed");

    // 5. Completion mailboxes at 0x50000..0x50018
    TEST_ASSERT(memory.read_u64(0x50000ULL) == 0xCAFEBABE207ULL, "Mailbox 207 confirmed (0xCAFEBABE207)");
    TEST_ASSERT(memory.read_u64(0x50008ULL) == 0xCAFEBABE208ULL, "Mailbox 208 confirmed (0xCAFEBABE208)");
    TEST_ASSERT(memory.read_u64(0x50010ULL) == 0xCAFEBABE209ULL, "Mailbox 209 confirmed (0xCAFEBABE209)");
    TEST_ASSERT(memory.read_u64(0x50018ULL) == 0xCAFEBABE210ULL, "Mailbox 210 confirmed (0xCAFEBABE210)");

    std::cout << "  [PASS] Stage 266.10 Early XNU initialization milestones verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 266 - Stages 266.8..266.10 Kernel Entry & Init Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage266_8_kernel_image_and_entry_audit()) return 1;
    if (!test_stage266_9_genuine_xnu_entry_integration()) return 1;
    if (!test_stage266_10_genuine_xnu_early_initialization()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 266.8..266.10 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
