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

void test_cpu_feature_initialization() {
    std::cout << "[Test 1] Stage 207.13: CPU Feature Initialization & System Registers...\n";

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

    // Execute genuine bootstrap sequence
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    // Verify CPACR_EL1.FPEN was configured to 0b11 (0x300000) by compiled assembly
    const std::uint64_t cpacr = cpu.get_cpacr_el1();
    std::cout << "  -> CPACR_EL1: 0x" << std::hex << cpacr << std::dec << "\n";
    TEST_CHECK((cpacr & 0x300000ULL) == 0x300000ULL, "CPACR_EL1 enables FP/NEON (FPEN=3)");

    // Verify PSTATE privilege level is EL1h
    TEST_CHECK(cpu.get_current_el() == 1, "Privilege level is EL1");
    TEST_CHECK(cpu.get_sp_sel() == true, "SP_EL1 selected (EL1h mode)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.13 CPU Feature Initialization Tests ===\n";
    test_cpu_feature_initialization();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.13: All CPU feature initialization tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.13: " << g_failures << " tests failed.\n";
        return 1;
    }
}
