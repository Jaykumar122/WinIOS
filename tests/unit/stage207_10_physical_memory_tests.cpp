#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/kernel_memory_layout.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;
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

void test_physical_memory_and_stealer() {
    std::cout << "[Test 1] Stage 207.10: Physical Memory & Early Page Allocator Stealing...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image();
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

    // Execute genuine arm_init
    for (std::size_t step = 0; step < 1000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    // Verify early page table root was allocated at 16KB boundary from topOfKernelData
    // In g_arm_init_state (at 0x10000), offset 16 contains l1_page_table_pa
    const std::uint64_t l1_pt_pa = memory.read_u64(0x10010ULL);
    std::cout << "  -> L1 Page Table Root PA: 0x" << std::hex << l1_pt_pa << std::dec << "\n";
    TEST_CHECK(l1_pt_pa != 0, "L1 page table root allocated");
    TEST_CHECK((l1_pt_pa & 0x3FFFULL) == 0, "L1 page table root is 16KB aligned");

    // Verify the L1 descriptor value written by arm_vm_init_early
    const std::uint64_t l1_desc = memory.read_u64(static_cast<std::size_t>(l1_pt_pa));
    std::cout << "  -> L1 Block Descriptor: 0x" << std::hex << l1_desc << std::dec << "\n";
    TEST_CHECK((l1_desc & 0x3) == 0x1, "L1 descriptor is valid block mapping");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.10 Physical Memory Initialization Tests ===\n";
    test_physical_memory_and_stealer();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.10: All physical memory tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.10: " << g_failures << " tests failed.\n";
        return 1;
    }
}
