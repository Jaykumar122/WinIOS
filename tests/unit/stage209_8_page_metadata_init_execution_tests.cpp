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

void test_page_metadata_init_execution() {
    std::cout << "[Test 1] Stage 209.8: Page Metadata Initialization Guest Execution...\n";

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

    std::cout << "  -> Total instructions retired in Campaign 209 guest execution: " << retired << "\n";
    TEST_CHECK(retired >= 600, "Instruction retirement expanded past Campaign 208 baseline (>586)");
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution reached architectural WFI");

    // 1. Verify pmap_init state in g_arm_init_state
    const std::uint32_t pmap_state = memory.read_u32(0x10038ULL);
    std::cout << "  -> pmap_initialized state: " << pmap_state << " (expected 2)\n";
    TEST_CHECK(pmap_state == 2, "pmap_init completed (pmap_initialized = 2)");

    // 2. Verify vm_page_bootstrap state in g_arm_init_state
    const std::uint32_t vm_page_state = memory.read_u32(0x1003CULL);
    std::cout << "  -> vm_page_bootstrap_done: " << vm_page_state << " (expected 1)\n";
    TEST_CHECK(vm_page_state == 1, "vm_page_bootstrap completed (vm_page_bootstrap_done = 1)");

    // 3. Verify Page Metadata Header at 0x28000
    const std::uint32_t hdr_magic = memory.read_u32(0x28000ULL);
    const std::uint32_t total_pages = memory.read_u32(0x28004ULL);
    const std::uint32_t managed_pages = memory.read_u32(0x28008ULL);
    const std::uint32_t free_pages = memory.read_u32(0x2800CULL);
    const std::uint64_t avail_st = memory.read_u64(0x28010ULL);
    const std::uint64_t avail_en = memory.read_u64(0x28018ULL);
    std::cout << "  -> Header avail_start: 0x" << std::hex << avail_st << ", avail_end: 0x" << avail_en << std::dec << "\n";

    TEST_CHECK(hdr_magic == 0x584E5550u, "Header magic is 'XNUP' (0x584E5550)");
    TEST_CHECK(total_pages == 512, "Header total_pages = 512");
    TEST_CHECK(managed_pages == 496, "Header managed_pages = 496");
    TEST_CHECK(free_pages == 496, "Header free_pages = 496");
    TEST_CHECK(avail_st == 0x38000ULL, "Header avail_start = 0x38000 (topOfKernelData + 32KB stolen tables)");
    TEST_CHECK(avail_en == 0x800000ULL, "Header avail_end = 0x800000");

    // 4. Verify Physical Attributes Table at 0x28020
    const std::uint8_t page0_attr = memory.read_u8(0x28020ULL);
    const std::uint8_t page15_attr = memory.read_u8(0x28020ULL + 15);
    const std::uint8_t page16_attr = memory.read_u8(0x28020ULL + 16);
    const std::uint8_t page511_attr = memory.read_u8(0x28020ULL + 511);

    TEST_CHECK(page0_attr == 0x00u, "Frame 0 attribute is unmanaged (reserved boot)");
    TEST_CHECK(page15_attr == 0x00u, "Frame 15 attribute is unmanaged (reserved stolen tables)");
    TEST_CHECK(page16_attr == 0x04u, "Frame 16 attribute is XNU_PHYS_MANAGED (0x04)");
    TEST_CHECK(page511_attr == 0x04u, "Frame 511 attribute is XNU_PHYS_MANAGED (0x04)");

    // 5. Verify Page Descriptors Array at 0x28220
    const std::uint32_t first_pfn = memory.read_u32(0x28220ULL);
    const std::uint8_t first_state = memory.read_u8(0x28220ULL + 6);
    const std::uint32_t last_pfn = memory.read_u32(0x28220ULL + (495 * 8));

    TEST_CHECK(first_pfn == 16, "First descriptor tracks PFN 16");
    TEST_CHECK(first_state == 0, "First descriptor state is FREE (0)");
    TEST_CHECK(last_pfn == 511, "Last descriptor tracks PFN 511");

    // 6. Verify Status Mailbox 209 at 0x50010
    const std::uint64_t mailbox209 = memory.read_u64(0x50010ULL);
    std::cout << "  -> Mailbox 209 at 0x50010: 0x" << std::hex << mailbox209 << std::dec << "\n";
    TEST_CHECK(mailbox209 == 0xCAFEBABE209ULL, "Mailbox 209 contains 0xCAFEBABE209");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.8 Page Metadata Init Execution Tests ===\n";
    test_page_metadata_init_execution();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.8: All page metadata init execution tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.8: " << g_failures << " tests failed.\n";
        return 1;
    }
}
