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

void test_genuine_xnu_vm_expansion() {
    std::cout << "[Test 1] Stage 209.18: Genuine XNU VM Execution Expansion Beyond pmap_bootstrap...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x800000); // 8 MiB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_209();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Campaign 209 Mach-O image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_res.success, "Kernel boot coordinator succeeded");

    std::size_t retired = 0;
    for (std::size_t step = 0; step < 10000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
        ++retired;
    }

    std::cout << "  -> Total instructions retired in Campaign 209 expansion: " << retired << "\n";
    TEST_CHECK(retired >= 600, "Instruction count strictly expanded beyond Campaign 208 baseline (>586)");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI");

    // 1. Verify pmap_init milestone reached (state = 2)
    const std::uint32_t pmap_state = memory.read_u32(0x10038ULL);
    TEST_CHECK(pmap_state == 2, "pmap_init completed in guest execution");

    // 2. Verify vm_page_bootstrap milestone reached (state = 1)
    const std::uint32_t vm_page_state = memory.read_u32(0x1003CULL);
    TEST_CHECK(vm_page_state == 1, "vm_page_bootstrap completed in guest execution");

    // 3. Verify Mailbox 207, 208, and 209 markers in memory
    const std::uint64_t mb207 = memory.read_u64(0x50000ULL);
    const std::uint64_t mb208 = memory.read_u64(0x50008ULL);
    const std::uint64_t mb209 = memory.read_u64(0x50010ULL);

    TEST_CHECK(mb207 == 0xCAFEBABE207ULL, "Mailbox 207 preserved (0xCAFEBABE207)");
    TEST_CHECK(mb208 == 0xCAFEBABE208ULL, "Mailbox 208 preserved (0xCAFEBABE208)");
    TEST_CHECK(mb209 == 0xCAFEBABE209ULL, "Mailbox 209 reached (0xCAFEBABE209)");

    // 4. Verify Corrected AIC2 Descriptor at Level-2 Slot 285
    const std::uint64_t pt_base = memory.read_u64(0x10010ULL); // l1_page_table_pa
    TEST_CHECK(pt_base >= 0x20000ULL, "Page table base is 16KB aligned in stolen DRAM");
    const std::uint64_t aic_desc = memory.read_u64(pt_base + (285 * 8));
    std::cout << "  -> Guest-written AIC2 descriptor at slot 285: 0x" << std::hex << aic_desc << std::dec << "\n";
    TEST_CHECK(aic_desc == 0x000000023A000705ULL, "Guest page table contains corrected AIC2 descriptor 0x023A000705");

    // 5. Verify Invalidated Ghost Memory at Level-2 Slot 1
    const std::uint64_t slot1_desc = memory.read_u64(pt_base + 8);
    TEST_CHECK(slot1_desc == 0x0ULL, "Level-2 slot 1 is 0x0 (invalidated ghost memory)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.18 Genuine XNU VM Expansion Tests ===\n";
    test_genuine_xnu_vm_expansion();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.18: All genuine XNU VM expansion tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.18: " << g_failures << " tests failed.\n";
        return 1;
    }
}
