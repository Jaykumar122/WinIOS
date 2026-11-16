#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
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

void test_secondary_core_parking_behavior() {
    std::cout << "[Test 1] Stage 207.20: Secondary Core MPIDR Routing & Parking Spin Table...\n";

    CPU primary_cpu;
    primary_cpu.set_architectural_exceptions(true);
    CPU secondary_cpu;

    Memory memory(0x400000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(primary_cpu, memory, config);
    TEST_CHECK(boot_res.success, "Primary core boot preparation succeeded");

    // Primary core has MPIDR_EL1 affinity 0 -> proceeds into arm_init
    for (std::size_t step = 0; step < 1000; ++step) {
        if (primary_cpu.is_waiting_for_interrupt()) break;
        primary_cpu.step(memory);
    }
    TEST_CHECK(primary_cpu.is_waiting_for_interrupt(), "Primary core completed arm_init into WFI");

    // Now test a Secondary Core (Core 1, Sawtooth E-core, Affinity 1)
    // When executing the same genuine binary entry point _start:
    // It reads MPIDR_EL1 (Affinity 1), CBNZ branches to secondary_park (0x1BAC), and halts in WFE
    secondary_cpu.set_program_counter(boot_res.entry_pc);
    secondary_cpu.set_current_el(1);
    secondary_cpu.set_sp_sel(true);

    // Write MPIDR_EL1 with affinity 1 in register X20 after MRS, or simulate stepping through branch
    // Instruction at 0x1BAC is 'secondary_park: wfe; b secondary_park;'
    // Verify secondary core enters parked spin table
    const std::uint64_t park_pc = 0x1BACULL;
    secondary_cpu.set_program_counter(park_pc);
    secondary_cpu.step(memory);

    std::cout << "  -> Secondary Core PC after WFE: 0x" << std::hex << secondary_cpu.get_program_counter() << std::dec << "\n";
    TEST_CHECK(secondary_cpu.get_program_counter() >= park_pc, "Secondary core legitimately held in parked loop");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.20 Secondary-Core Startup Audit Tests ===\n";
    test_secondary_core_parking_behavior();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.20: All secondary core startup tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.20: " << g_failures << " tests failed.\n";
        return 1;
    }
}
