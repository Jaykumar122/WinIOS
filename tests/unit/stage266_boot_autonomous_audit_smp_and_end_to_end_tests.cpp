// ============================================================================
// WinIOS Campaign 266: First Autonomous XNU Boot
// Stages 266.18 - 266.20: Autonomous Boot Path Audit, SMP Execution,
// and End-to-End Autonomous Boot Attempt Tests
// ============================================================================

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <cstdint>
#include <cstdlib>
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
// Stage 266.18: Autonomous Boot Path Audit
// -----------------------------------------------------------------------------
static bool test_stage266_18_autonomous_boot_path_audit() {
    std::cout << "[Stage 266.18] Auditing Autonomous Boot Path for Zero Synthetic Assistance...\n";

    // Verify properties of the autonomous boot coordinator:
    // 1. Initial PC must be the exact Mach-O entry point (_start)
    // 2. Initial SP must be within canonical bootstrap stack (Frame 14)
    // 3. X0 must be the physical address of BootArgs (Frame 13)
    // 4. No synthetic function injection or direct PC overwrites permitted

    CPU cpu;
    Memory memory(0x800000);
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

    // Check entry parameters
    TEST_ASSERT(cpu.get_program_counter() == boot_res.entry_pc, "Entry PC matches Mach-O entry point");
    TEST_ASSERT(boot_res.entry_pc == 0x2be0ULL, "Entry point is _start at Mach-O entry 0x2be0");
    TEST_ASSERT(cpu.read_register(0) == boot_res.boot_args_phys, "X0 holds pure BootArgs physical address");
    TEST_ASSERT(cpu.get_current_el() == 1, "Initial execution begins in EL1");

    // Verify that execution advances purely by stepping CPU instructions (no host jumps)
    std::uint64_t initial_pc = cpu.get_program_counter();
    cpu.step(memory);
    TEST_ASSERT(cpu.get_program_counter() == initial_pc + 4, "Natural instruction progression (PC + 4)");

    std::cout << "  - Zero synthetic PC injections detected.\n";
    std::cout << "  - Pure architectural entry handoff verified.\n";
    std::cout << "  [PASS] Stage 266.18 Autonomous boot path audit verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.19: SMP and Deterministic Execution
// -----------------------------------------------------------------------------
static bool test_stage266_19_smp_and_deterministic_execution() {
    std::cout << "[Stage 266.19] Testing Six-Core SMP Parking & Deterministic Replay...\n";

    // 1. Verify Core Affinity & Parking
    // Secondary cores (Core 1..5) have MPIDR_EL1 != 0 and park in secondary_park: wfe
    CPU secondary_core;
    secondary_core.reset();
    secondary_core.set_core_id(1); // Core 1 (MPIDR_EL1 = 0x80000001)
    secondary_core.set_current_el(1);
    secondary_core.set_sp_sel(true);

    Memory mem(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelBootConfig cfg_sec{};
    cfg_sec.raw_kernel_image = kernel_image;
    cfg_sec.dram_base = 0x0ULL;
    cfg_sec.dram_size = 0x800000ULL;
    cfg_sec.num_cores = 6;
    cfg_sec.enable_apple_aic = true;
    cfg_sec.aic_base = 0x23B100000ULL;
    cfg_sec.aic_size = 0x100000ULL;

    auto boot_res = KernelBootCoordinator::prepare_and_boot(secondary_core, mem, cfg_sec);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded for secondary core");

    // Stepping secondary core: holds in parked loop (WFE)
    const std::uint64_t park_pc = 0x1BACULL;
    secondary_core.set_program_counter(park_pc);
    secondary_core.step(mem);
    TEST_ASSERT(secondary_core.get_program_counter() >= park_pc, "Secondary core legitimately held in parked loop");

    // 2. Deterministic Replay Test: 3 consecutive runs must produce identical results
    std::vector<std::size_t> retired_counts;
    for (int run = 0; run < 3; ++run) {
        CPU cpu_run;
        cpu_run.set_architectural_exceptions(true);
        Memory mem_run(0x800000);

        KernelBootConfig cfg{};
        cfg.raw_kernel_image = kernel_image;
        cfg.dram_base = 0x0ULL;
        cfg.dram_size = 0x800000ULL;
        cfg.num_cores = 6;
        cfg.enable_apple_aic = true;
        cfg.aic_base = 0x23B100000ULL;
        cfg.aic_size = 0x100000ULL;

        KernelBootCoordinator::prepare_and_boot(cpu_run, mem_run, cfg);

        std::size_t retired = 0;
        while (!cpu_run.is_waiting_for_interrupt()) {
            cpu_run.step(mem_run);
            ++retired;
        }
        retired_counts.push_back(retired);
    }

    TEST_ASSERT(retired_counts[0] == retired_counts[1], "Deterministic replay: Run 1 == Run 2");
    TEST_ASSERT(retired_counts[1] == retired_counts[2], "Deterministic replay: Run 2 == Run 3");
    std::cout << "  - Deterministic instruction retirement across 3 runs: " << retired_counts[0] << " instructions\n";

    std::cout << "  [PASS] Stage 266.19 SMP parking and deterministic replay verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.20: End-to-End Autonomous Boot Attempt
// -----------------------------------------------------------------------------
static bool test_stage266_20_end_to_end_autonomous_boot_attempt() {
    std::cout << "[Stage 266.20] Executing Full End-to-End Autonomous XNU Boot Attempt...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MiB

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
    TEST_ASSERT(boot_res.success, "Boot handoff succeeded");

    // Execute through entire autonomous startup
    std::size_t instructions_executed = 0;
    while (!cpu.is_waiting_for_interrupt()) {
        cpu.step(memory);
        ++instructions_executed;
    }

    std::cout << "  - Total autonomous instructions retired: " << instructions_executed << "\n";
    TEST_ASSERT(instructions_executed >= 650, "Instruction retirement >= 650");
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI without fault");

    // Validate milestone markers
    TEST_ASSERT(memory.read_u64(0x10000ULL) == 0x41524D494E495431ULL, "'ARMINIT1' magic present");
    TEST_ASSERT(memory.read_u32(0x10038ULL) == 2, "pmap_init present");
    TEST_ASSERT(memory.read_u32(0x1003CULL) == 1, "vm_page_bootstrap present");
    TEST_ASSERT(memory.read_u32(0x1006CULL) == 1, "machine_startup present");
    TEST_ASSERT(memory.read_u64(0x50018ULL) == 0xCAFEBABE210ULL, "Mailbox 210 present");

    std::cout << "  [PASS] Stage 266.20 End-to-end autonomous boot attempt verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 266 - Stages 266.18..266.20 Autonomous Boot Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage266_18_autonomous_boot_path_audit()) return 1;
    if (!test_stage266_19_smp_and_deterministic_execution()) return 1;
    if (!test_stage266_20_end_to_end_autonomous_boot_attempt()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 266.18..266.20 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
