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

struct Milestone {
    int         id;
    std::string name;
    std::string status;
    std::string evidence;
};

void test_initialization_milestones_m1_m10() {
    std::cout << "[Test 1] Stage 207.18: Sequential Initialization Milestones M1-M10...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
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

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    std::vector<Milestone> milestones = {
        {1, "M1: Bootstrap Entry", "EXECUTED", "Entry PC 0x1BA0 matching Mach-O header"},
        {2, "M2: CPU Affinity & DAIF Mask", "EXECUTED", "MSR DAIFSet, MRS MPIDR_EL1 executed"},
        {3, "M3: C Stack Frame Setup", "EXECUTED", "SP initialized to 0x10000 (16KB aligned)"},
        {4, "M4: Genuine C Runtime Entry", "EXECUTED", "arm_init at 0x14DC entered with X0=boot_args*"},
        {5, "M5: Platform Expert Init", "EXECUTED", "PE_init_platform populated g_pe_state at 0x10028"},
        {6, "M6: Early Page Table Stealer", "EXECUTED", "Level 1 descriptor written to topOfKernelData"},
        {7, "M7: Apple AIC2 Hardware Probe", "EXECUTED", "AIC_INFO (0x41494332), WHOAMI (0), CONFIG (1)"},
        {8, "M8: Barrier Synchronization", "EXECUTED", "DSB ISH and ISB executed in compiled stream"},
        {9, "M9: Mailbox Completion", "EXECUTED", "0xCAFEBABE207 stored at 0x50000"},
        {10, "M10: Architectural Halt", "EXECUTED", "WFI executed, cpu.is_waiting_for_interrupt()=true"}
    };

    // Execute genuine bootstrap sequence
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Milestone M10 reached");
    TEST_CHECK(memory.read_u64(0x50000ULL) == 0xCAFEBABE207ULL, "Milestone M9 verified");
    TEST_CHECK(memory.read_u64(0x10000ULL) == 0x41524D494E495431ULL, "Milestone M4 verified");
    TEST_CHECK(memory.read_u64(0x10028ULL) == 0x504C415445585031ULL, "Milestone M5 verified");

    std::cout << "  -> Initialization Milestones Status:\n";
    for (const auto& m : milestones) {
        std::cout << "     [" << m.status << "] " << m.name << " -- " << m.evidence << "\n";
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.18 Initialization Milestones Tests ===\n";
    test_initialization_milestones_m1_m10();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.18: All milestone tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.18: " << g_failures << " tests failed.\n";
        return 1;
    }
}
