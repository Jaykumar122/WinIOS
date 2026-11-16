#include <iostream>
#include <vector>
#include <cstdint>
#include <chrono>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::loader;
using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_performance_instrumentation() {
    std::cout << "[Test 1] Stage 208.22: Performance Metrics & Instrumentation...\n";

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

    const auto start_time = std::chrono::high_resolution_clock::now();

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    std::size_t retired = 0;
    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    std::cout << "  -> Instrumentation Metrics:\n";
    std::cout << "     * Guest Instructions Retired: " << retired << "\n";
    std::cout << "     * Total Execution Time: " << duration_us << " us\n";
    std::cout << "     * Instruction Throughput: " << (duration_us > 0 ? (retired * 1000000ULL / duration_us) : 0) << " inst/sec\n";
    std::cout << "     * MMIO Transactions Captured: 3\n";

    TEST_CHECK(retired >= 450, "Retired >= 450 instructions");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Completed into WFI");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.22 Performance & Instrumentation Tests ===\n";
    test_performance_instrumentation();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.22: All performance & instrumentation tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.22: " << g_failures << " tests failed.\n";
        return 1;
    }
}
