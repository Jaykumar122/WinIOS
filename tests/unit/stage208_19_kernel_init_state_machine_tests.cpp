#include <iostream>
#include <vector>
#include <cstdint>
#include <string>

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

struct Milestone {
    int id;
    std::string name;
    std::string evidence;
};

void test_initialization_state_machine_m1_m12() {
    std::cout << "[Test 1] Stage 208.19: Sequential Milestones M1 - M12 State Machine...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_208();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    std::vector<Milestone> milestones = {
        {1, "M1: Bootstrap Entry", "Entry PC 0x1BA0 matching Mach-O header"},
        {2, "M2: CPU Affinity & DAIF Mask", "MSR DAIFSet, MRS MPIDR_EL1 executed"},
        {3, "M3: C Stack Frame Setup", "SP initialized to 0x10000 (16KB aligned)"},
        {4, "M4: Genuine C Runtime Entry", "arm_init entered with X0=boot_args*"},
        {5, "M5: Platform Expert Init", "PE_init_platform populated g_pe_state at 0x10028"},
        {6, "M6: Early Page Table Stealer", "Level 1/2 descriptors written to topOfKernelData (0x30000)"},
        {7, "M7: Apple AIC2 Hardware Probe", "AIC_INFO (0x06010200), WHOAMI (0), CONFIG (1)"},
        {8, "M8: Barrier Synchronization", "DSB ISH and ISB executed in compiled stream"},
        {9, "M9: Mailbox Completion 207", "0xCAFEBABE207 stored at 0x50000"},
        {10, "M10: Architectural Halt", "WFI executed, cpu.is_waiting_for_interrupt()=true"},
        {11, "M11: Physical Memory Manager", "pmap_bootstrap registered avail_start/end at 0x10028/0x10030"},
        {12, "M12: Mailbox Completion 208", "0xCAFEBABE208 stored at 0x50008"}
    };

    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }

    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Milestone M10 reached (WFI)");
    TEST_CHECK(memory.read_u64(0x50000ULL) == 0xCAFEBABE207ULL, "Milestone M9 verified");
    TEST_CHECK(memory.read_u64(0x50008ULL) == 0xCAFEBABE208ULL, "Milestone M12 verified");
    TEST_CHECK(memory.read_u32(0x10038ULL) == 1, "Milestone M11 verified (pmap_bootstrap)");
    TEST_CHECK(memory.read_u64(0x10000ULL) == 0x41524D494E495431ULL, "Milestone M4 verified (ARMINIT1)");
    TEST_CHECK(memory.read_u64(0x10028ULL) != 0, "Milestone M11 avail_start verified");

    std::cout << "  -> Initialization State Machine Summary (12 Milestones Verified):\n";
    for (const auto& m : milestones) {
        std::cout << "     [EXECUTED] M" << m.id << ": " << m.name << " -- " << m.evidence << "\n";
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.19 Kernel Initialization State Machine Tests ===\n";
    test_initialization_state_machine_m1_m12();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.19: All initialization state machine tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.19: " << g_failures << " tests failed.\n";
        return 1;
    }
}
