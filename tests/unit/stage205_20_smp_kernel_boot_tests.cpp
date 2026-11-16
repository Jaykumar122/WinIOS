#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "runtime/smp_kernel_boot_manager.h"

using namespace winios;
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

void test_smp_initialization_and_topology() {
    std::cout << "[Test 1] SMP Kernel Boot Manager Topology and Per-Core Config..." << std::endl;

    SmpKernelBootManager smp;
    constexpr std::size_t kNumCores = 6; // iPhone 15: 2 Everest + 4 Sawtooth
    constexpr std::uint64_t kEntryPC = 0x1000ULL;
    constexpr std::uint64_t kBootArgsPA = 0x40000ULL;
    constexpr std::uint64_t kStackPoolBase = 0x100000ULL;
    constexpr std::uint64_t kVbarBase = 0x2000ULL;

    const bool init_ok = smp.initialize(kNumCores, kEntryPC, kBootArgsPA, kStackPoolBase, kVbarBase);
    TEST_CHECK(init_ok, "SMP initialization must succeed");
    TEST_CHECK(smp.num_cores() == 6, "Total cores must be 6");
    TEST_CHECK(smp.online_cores() == 0, "Initial online cores must be 0");

    // Verify Core 0 (Everest P-Core)
    const auto* core0 = smp.get_core_config(0);
    TEST_CHECK(core0 != nullptr, "Core 0 config must exist");
    if (core0) {
        TEST_CHECK(core0->is_primary == true, "Core 0 must be primary");
        TEST_CHECK(core0->is_performance_core == true, "Core 0 must be P-core");
        TEST_CHECK(core0->initial_sp == (kStackPoolBase + 0x4000ULL), "Core 0 initial SP mismatch");
        TEST_CHECK((core0->initial_sp % 16) == 0, "Core 0 SP must be 16-byte aligned");
        TEST_CHECK(core0->entry_pc == kEntryPC, "Core 0 entry PC mismatch");
    }

    // Verify Core 1 (Everest P-Core)
    const auto* core1 = smp.get_core_config(1);
    TEST_CHECK(core1 != nullptr, "Core 1 config must exist");
    if (core1) {
        TEST_CHECK(core1->is_primary == false, "Core 1 must be secondary");
        TEST_CHECK(core1->is_performance_core == true, "Core 1 must be P-core");
        TEST_CHECK(core1->initial_sp == (kStackPoolBase + 0x8000ULL), "Core 1 initial SP mismatch");
    }

    // Verify Core 2..5 (Sawtooth E-Cores)
    for (std::size_t i = 2; i < 6; ++i) {
        const auto* core = smp.get_core_config(i);
        TEST_CHECK(core != nullptr, "Core config must exist");
        if (core) {
            TEST_CHECK(core->is_primary == false, "Core must be secondary");
            TEST_CHECK(core->is_performance_core == false, "Core must be E-core");
            TEST_CHECK((core->mpidr_el1 & 0x0100ULL) != 0, "E-core must report cluster 1 in MPIDR");
        }
    }
}

void test_primary_and_secondary_boot_progression() {
    std::cout << "[Test 2] Primary and Secondary Core Bring-up and Rendezvous..." << std::endl;

    Memory memory(0x400000); // 4 MB RAM
    SmpKernelBootManager smp;

    constexpr std::size_t kNumCores = 6;
    constexpr std::uint64_t kEntryPC = 0x1000ULL;
    constexpr std::uint64_t kBootArgsPA = 0x40000ULL;
    constexpr std::uint64_t kStackPoolBase = 0x100000ULL;
    constexpr std::uint64_t kVbarBase = 0x2000ULL;

    smp.initialize(kNumCores, kEntryPC, kBootArgsPA, kStackPoolBase, kVbarBase);

    // 1. Boot Primary Core 0
    CPU cpu0;
    const bool boot_p_ok = smp.boot_primary_core(cpu0, memory);
    TEST_CHECK(boot_p_ok, "Boot primary core must succeed");
    TEST_CHECK(smp.online_cores() == 1, "Only 1 core online after primary boot");
    TEST_CHECK(smp.check_rendezvous(memory) == false, "Rendezvous must not be complete with only primary online");

    TEST_CHECK(cpu0.get_current_el() == 1, "Primary core must be EL1");
    TEST_CHECK(cpu0.get_sp_sel() == true, "Primary core must use SP_EL1");
    TEST_CHECK(cpu0.read_register(0) == kBootArgsPA, "Primary core X0 must hold boot_args");
    TEST_CHECK(cpu0.read_register(19) == kBootArgsPA, "Primary core X19 must hold boot_args");

    // 2. Bring up Secondary Cores 1..5
    std::vector<CPU> secondary_cpus(5);
    for (std::size_t i = 1; i < 6; ++i) {
        const bool bring_up_ok = smp.bring_up_secondary_core(i, secondary_cpus[i - 1], memory);
        TEST_CHECK(bring_up_ok, "Bring up secondary core must succeed");

        TEST_CHECK(secondary_cpus[i - 1].get_current_el() == 1, "Secondary core must be EL1");
        TEST_CHECK(secondary_cpus[i - 1].read_register(0) == i, "Secondary core X0 must hold core_id");
        TEST_CHECK(secondary_cpus[i - 1].read_register(1) == kBootArgsPA, "Secondary core X1 must hold boot_args");
    }

    TEST_CHECK(smp.online_cores() == 6, "All 6 cores must be online");
    TEST_CHECK(smp.check_rendezvous(memory) == true, "All 6 cores must satisfy shared rendezvous");
}

void test_smp_concurrent_execution() {
    std::cout << "[Test 3] SMP Per-Core Instruction Execution and Stepping..." << std::endl;

    Memory memory(0x400000);
    SmpKernelBootManager smp;

    constexpr std::uint64_t kEntryPC = 0x1000ULL;
    // Write simple instruction sequence at kEntryPC:
    // NOP (0xD503201F)
    // ADD X2, X2, #1 (0x91000442)
    memory.write_u32(0x1000, 0xD503201FU);
    memory.write_u32(0x1004, 0x91000442U);

    smp.initialize(2, kEntryPC, 0x40000ULL, 0x100000ULL, 0x2000ULL);

    CPU cpu0;
    CPU cpu1;
    smp.boot_primary_core(cpu0, memory);
    smp.bring_up_secondary_core(1, cpu1, memory);

    // Step each core 2 instructions
    smp.step_core(0, cpu0, memory);
    smp.step_core(0, cpu0, memory);

    smp.step_core(1, cpu1, memory);
    smp.step_core(1, cpu1, memory);

    TEST_CHECK(smp.get_core_config(0)->instructions_retired == 2, "Core 0 must have retired 2 instructions");
    TEST_CHECK(smp.get_core_config(1)->instructions_retired == 2, "Core 1 must have retired 2 instructions");

    TEST_CHECK(cpu0.get_program_counter() == 0x1008, "Core 0 PC advanced to 0x1008");
    TEST_CHECK(cpu1.get_program_counter() == 0x1008, "Core 1 PC advanced to 0x1008");
    TEST_CHECK(cpu0.read_register(2) == 1, "Core 0 X2 incremented");
    TEST_CHECK(cpu1.read_register(2) == 1, "Core 1 X2 incremented");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.20 SMP Kernel Boot Foundation Tests ===" << std::endl;

    test_smp_initialization_and_topology();
    test_primary_and_secondary_boot_progression();
    test_smp_concurrent_execution();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.20 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.20 Tests FAILED ===" << std::endl;
        return 1;
    }
}
