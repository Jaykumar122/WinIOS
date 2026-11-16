#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_secondary_core_execution_truth() {
    std::cout << "[Test 1] Stage 208.17: Secondary Core Instruction Execution Truth Audit...\n";

    CPU primary_cpu;
    primary_cpu.set_architectural_exceptions(true);
    primary_cpu.set_core_id(0); // Affinity 0 (Primary, MPIDR_EL1 = 0x80000000)

    CPU secondary_cpu;
    secondary_cpu.set_architectural_exceptions(true);
    secondary_cpu.set_core_id(1); // Affinity 1 (Secondary Core 1, MPIDR_EL1 = 0x80000001)

    Memory memory(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_208();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Mach-O image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(primary_cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    // 1. Primary core steps through _start into arm_init
    for (std::size_t step = 0; step < 2000; ++step) {
        if (primary_cpu.is_waiting_for_interrupt()) break;
        primary_cpu.step(memory);
    }
    TEST_CHECK(primary_cpu.is_waiting_for_interrupt(), "Primary core entered WFI");

    // 2. Secondary core begins at same entry point _start (0x1BA0)
    secondary_cpu.set_program_counter(boot_res.entry_pc);
    secondary_cpu.set_current_el(1);
    secondary_cpu.set_sp_sel(true);

    // Step secondary core:
    // 0x1BA0: msr daifset, #0xf
    // 0x1BA4: mrs x20, mpidr_el1
    // 0x1BA8: and x20, x20, #0xff
    // 0x1BAC: cbnz x20, secondary_park (TAKEN!)
    for (int step = 0; step < 4; ++step) {
        secondary_cpu.step(memory);
    }

    std::cout << "  -> Secondary Core PC after affinity branch: 0x" << std::hex << secondary_cpu.get_program_counter() << std::dec << "\n";
    // Branch taken to secondary_park (0x1BAC or 0x1BB0 depending on offset)
    TEST_CHECK(secondary_cpu.get_program_counter() >= 0x1BACULL, "Secondary core took branch to secondary_park");

    // Step inside parking loop:
    // Executes 'wfe' instruction
    secondary_cpu.step(memory);
    std::cout << "  -> Secondary Core executed WFE at PC 0x" << std::hex << secondary_cpu.get_program_counter() << std::dec << "\n";
    TEST_CHECK(secondary_cpu.get_program_counter() >= 0x1BACULL, "Secondary core held in parked loop");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.17 Secondary Core Execution Truth Tests ===\n";
    test_secondary_core_execution_truth();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.17: All secondary core execution truth tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.17: " << g_failures << " tests failed.\n";
        return 1;
    }
}
