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

void test_bootstrap_vm_execution() {
    std::cout << "[Test 1] Stage 208.11: Bootstrap Virtual Memory & pmap_bootstrap Execution...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x800000); // 8 MB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_208();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Mach-O image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 serial=3 cpus=6";
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot coordinator succeeded");

    std::size_t retired = 0;
    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total genuine instructions retired: " << retired << "\n";
    TEST_CHECK(retired >= 450, "Retired at least 450 genuine instructions");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution completed into WFI");

    // Verify pmap_bootstrap outputs in g_arm_init_state:
    // arm_init_state_t layout:
    // 0: arm_init_magic
    // 8: boot_args_addr
    // 16: l1_page_table_pa
    // 24: aic_info
    // 28: aic_whoami
    // 32: instructions_executed
    // 36: initialized
    // 40 (0x28): avail_start
    // 48 (0x30): avail_end
    // 56 (0x38): pmap_initialized
    const std::uint64_t avail_st = memory.read_u64(0x10028ULL);
    const std::uint64_t avail_en = memory.read_u64(0x10030ULL);
    const std::uint32_t pmap_init = memory.read_u32(0x10038ULL);

    std::cout << "  -> pmap_bootstrap avail_start: 0x" << std::hex << avail_st << std::dec << "\n";
    std::cout << "  -> pmap_bootstrap avail_end: 0x" << std::hex << avail_en << std::dec << "\n";
    std::cout << "  -> pmap_bootstrap initialized flag: " << pmap_init << "\n";

    TEST_CHECK(pmap_init == 1, "pmap_bootstrap marked pmap_initialized = 1");
    TEST_CHECK(avail_st >= 0x38000ULL, "avail_start allocated above kernel page tables");
    TEST_CHECK(avail_en == 0x800000ULL, "avail_end matches DRAM size (8MB)");

    // Verify Campaign 208 Mailbox
    const std::uint64_t mb208 = memory.read_u64(0x50008ULL);
    std::cout << "  -> Status Mailbox 208 at 0x50008: 0x" << std::hex << mb208 << std::dec << "\n";
    TEST_CHECK(mb208 == 0xCAFEBABE208ULL, "Mailbox 208 holds 0xCAFEBABE208");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.11 Bootstrap VM Execution Tests ===\n";
    test_bootstrap_vm_execution();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.11: All bootstrap VM execution tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.11: " << g_failures << " tests failed.\n";
        return 1;
    }
}
