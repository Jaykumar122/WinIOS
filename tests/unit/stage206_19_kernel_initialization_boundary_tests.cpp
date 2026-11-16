#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_bootstrap_continuation.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_initialization_milestones() {
    std::cout << "[Test 1] Kernel Initialization Milestones & Phase PC Boundary Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x400000);

    XnuContinuationConfig xnu_cfg{};
    xnu_cfg.text_vmaddr = 0x0ULL;
    xnu_cfg.entry_vaddr = 0x1000ULL;
    xnu_cfg.vbar_vaddr = 0x4000ULL;
    xnu_cfg.aic_base = 0x23B100000ULL;
    xnu_cfg.uart_base = 0x90000ULL;
    xnu_cfg.status_mailbox_pa = 0x20000ULL + 736ULL + 0x100ULL;

    auto image = XnuBootstrapContinuationBuilder::build_image(xnu_cfg);

    KernelBootConfig config{};
    config.raw_kernel_image = image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot preparation succeeded");

    // Phase Milestone 1: Entry Point Handoff
    TEST_CHECK(cpu.get_program_counter() == 0x1000ULL, "Milestone 1 reached: Entry PC 0x1000");

    // Step through Phase 1: Prologue
    cpu.step(memory); // MSR DAIFSet
    cpu.step(memory); // MOV X19, X0
    cpu.step(memory); // MRS X20, MPIDR_EL1
    TEST_CHECK(cpu.read_register(19) == boot_res.boot_args_phys, "Preserved boot_args pointer");
    TEST_CHECK(cpu.read_register(20) == 0x80000000ULL, "MPIDR captured Boot CPU 0");

    // Step until WFI
    std::size_t steps = 3;
    while (!cpu.is_waiting_for_interrupt() && steps < 100) {
        cpu.step(memory);
        ++steps;
    }

    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Milestone 5 reached: Completed bootstrap sequence at WFI");
    TEST_CHECK(memory.read_u64(static_cast<std::size_t>(xnu_cfg.status_mailbox_pa)) == 0xCAFEBABE206ULL,
               "Milestone 5 verified: Status mailbox written");

    std::cout << "  -> Kernel initialization milestones verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.19 Kernel Initialization Boundary Tests ===\n";
    test_kernel_initialization_milestones();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.19 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.19 Tests FAILED ===\n";
        return 1;
    }
}
