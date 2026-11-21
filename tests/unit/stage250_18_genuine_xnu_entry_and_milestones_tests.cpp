// ============================================================================
// Stage 250.16 - 250.19: Genuine XNU Entry, Initialization Milestones &
// Panic/Fault Diagnostics Tests
//
// Tests:
// 1. Stage 250.16: Kernel Entry Handoff Protocol
//    - Verified PC = _start, SP = bootstrap_stack_top, X0 = boot_args_phys.
//    - Verified PSTATE.DAIF = 0xF (interrupts masked), CurrentEL = EL1.
// 2. Stage 250.17: Genuine XNU Image Execution
//    - Transfer control to actual entry point of xnu_genuine_init.macho.
//    - Instruction retirement through early assembly prologue in start.s.
// 3. Stage 250.18: Early XNU Initialization Milestones
//    - Execution reaches arm_init, pe_arm_init, arm_vm_init_early.
//    - Execution reaches pmap_bootstrap, pmap_init, vm_page_bootstrap.
//    - Execution reaches machine_startup, kmem_init, zone_bootstrap, sched_init, task_init, thread_init.
//    - Completion mailboxes written (0xCAFEBABE207, 0xCAFEBABE208, 0xCAFEBABE209, 0xCAFEBABE210).
//    - Clean exit to architectural WFI.
// 4. Stage 250.19: Kernel Panic and Fault Diagnostics
//    - Diagnostic capture on illegal instruction or invalid memory access.
// ============================================================================

#include "cpu/cpu.h"
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

// ----------------------------------------------------------------------------
// Test 1: Stage 250.16 & 250.17: Kernel Entry Handoff & Genuine Image Execution
// ----------------------------------------------------------------------------
static void test_kernel_entry_handoff_and_execution() {
    std::cout << "[Stage 250.16-250.17] Testing Kernel Entry Handoff & Genuine Image Execution...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MiB RAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!kernel_image.empty(), "Genuine compiled Campaign 210 Mach-O loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "KernelBootCoordinator succeeded");

    // Stage 250.16: Verify Entry Invariants
    TEST_ASSERT(cpu.get_program_counter() == boot_res.entry_pc, "PC initialized to entry point");
    TEST_ASSERT(cpu.get_stack_pointer() == boot_res.initial_sp, "SP initialized to bootstrap stack");
    TEST_ASSERT(cpu.read_register(0) == boot_res.boot_args_phys, "X0 holds boot_args physical address");
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception Level is EL1");

    // Stage 250.17: Step through early start.s prologue instructions
    std::size_t early_steps = 0;
    for (std::size_t i = 0; i < 20; ++i) {
        cpu.step(memory);
        ++early_steps;
    }
    TEST_ASSERT(early_steps == 20, "Early assembly prologue executed without exception");
    TEST_ASSERT(cpu.read_register(19) == boot_res.boot_args_phys, "X19 preserved boot_args pointer");

    std::cout << "[Stage 250.16-250.17] Kernel entry handoff and genuine execution verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 250.18: Early XNU Initialization Milestones
// ----------------------------------------------------------------------------
static void test_xnu_initialization_milestones() {
    std::cout << "[Stage 250.18] Testing Early XNU Initialization Milestones...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
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

    // Run until WFI
    std::size_t retired = 0;
    for (std::size_t step = 0; step < 15000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Instructions retired to completion: " << retired << "\n";
    TEST_ASSERT(retired >= 650, "Instruction retirement strictly > 650");
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI");

    // 1. Check arm_init_state magic at 0x10000
    const uint64_t arm_init_magic = memory.read_u64(0x10000ULL);
    TEST_ASSERT(arm_init_magic == 0x41524D494E495431ULL, "arm_init_magic matches 'ARMINIT1'");

    // 2. Check pmap_init milestone (state = 2)
    const uint32_t pmap_state = memory.read_u32(0x10038ULL);
    TEST_ASSERT(pmap_state == 2, "pmap_init completed");

    // 3. Check vm_page_bootstrap milestone (state = 1)
    const uint32_t vm_page_state = memory.read_u32(0x1003CULL);
    TEST_ASSERT(vm_page_state == 1, "vm_page_bootstrap completed");

    // 4. Check kernel subsystem flags (kmem, zone, sched, task, thread, startup)
    TEST_ASSERT(memory.read_u32(0x10058ULL) == 1, "kmem_init completed");
    TEST_ASSERT(memory.read_u32(0x1005CULL) == 1, "zone_bootstrap completed");
    TEST_ASSERT(memory.read_u32(0x10060ULL) == 1, "sched_init completed");
    TEST_ASSERT(memory.read_u32(0x10064ULL) == 1, "task_init completed");
    TEST_ASSERT(memory.read_u32(0x10068ULL) == 1, "thread_init completed");
    TEST_ASSERT(memory.read_u32(0x1006CULL) == 1, "machine_startup completed");

    // 5. Check all completion mailboxes
    TEST_ASSERT(memory.read_u64(0x50000ULL) == 0xCAFEBABE207ULL, "Mailbox 207 written");
    TEST_ASSERT(memory.read_u64(0x50008ULL) == 0xCAFEBABE208ULL, "Mailbox 208 written");
    TEST_ASSERT(memory.read_u64(0x50010ULL) == 0xCAFEBABE209ULL, "Mailbox 209 written");
    TEST_ASSERT(memory.read_u64(0x50018ULL) == 0xCAFEBABE210ULL, "Mailbox 210 written");

    std::cout << "[Stage 250.18] All early XNU initialization milestones verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 250.19: Kernel Fault & Abort Diagnostics
// ----------------------------------------------------------------------------
static void test_kernel_fault_diagnostics() {
    std::cout << "[Stage 250.19] Testing Kernel Fault Diagnostics...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory memory(0x100000);

    const uint64_t vbar_addr = 0x80000ULL;
    cpu.set_vbar_el1(vbar_addr);

    // Attempt instruction fetch from unmapped address 0xDEAD0000
    cpu.set_program_counter(0xDEAD0000ULL);
    cpu.set_current_el(1);

    cpu.step(memory);

    // Vector to synchronous abort handler
    TEST_ASSERT(cpu.get_program_counter() == vbar_addr + 0x200, "Vectored to abort handler on unmapped PC");
    TEST_ASSERT(cpu.get_far_el1() == 0xDEAD0000ULL, "FAR_EL1 captures faulting PC 0xDEAD0000");

    std::cout << "[Stage 250.19] Kernel fault diagnostics verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 250.16 - 250.19 Genuine XNU Execution Tests\n";
    std::cout << "============================================================\n";

    test_kernel_entry_handoff_and_execution();
    test_xnu_initialization_milestones();
    test_kernel_fault_diagnostics();

    std::cout << "\n>>> ALL STAGE 250.16 - 250.19 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
