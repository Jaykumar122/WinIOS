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

void test_genuine_xnu_expansion() {
    std::cout << "[Test 1] Stage 208.18: Genuine XNU Function Call Expansion Beyond arm_init...\n";

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
    config.command_line = "-v debug=0x144 serial=3 cpus=6";
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    std::size_t retired = 0;
    bool hit_arm_init = false;
    bool hit_pe_init = false;
    bool hit_vm_init = false;

    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;

        const std::uint64_t pc = cpu.get_program_counter();
        if (pc == 0x1648ULL) hit_arm_init = true;
        if (pc == 0x1BACULL || pc == 0x1D84ULL) hit_pe_init = true;
        if (memory.read_u64(0x50008ULL) == 0xCAFEBABE208ULL) hit_vm_init = true;

        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total instructions retired in expansion: " << retired << "\n";
    TEST_CHECK(retired >= 450, "Instruction retirement expanded beyond baseline");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Reached WFI");

    TEST_CHECK(hit_arm_init, "Executed arm_init C entry");
    TEST_CHECK(hit_pe_init, "Executed PE_init_platform");
    TEST_CHECK(hit_vm_init, "Executed arm_vm_init_early");

    // Check pmap_bootstrap results
    const std::uint32_t pmap_flag = memory.read_u32(0x10038ULL);
    TEST_CHECK(pmap_flag == 1, "Executed pmap_bootstrap successfully");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.18 Genuine XNU Expansion Tests ===\n";
    test_genuine_xnu_expansion();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.18: All genuine XNU expansion tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.18: " << g_failures << " tests failed.\n";
        return 1;
    }
}
