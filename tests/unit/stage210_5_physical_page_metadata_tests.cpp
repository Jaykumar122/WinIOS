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

void test_physical_page_metadata_and_free_list() {
    std::cout << "[Test 1] Stage 210.5: Physical Page Metadata & Free-List Management Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000); // 8 MiB guest DRAM

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_CHECK(!kernel_image.empty(), "Genuine compiled Campaign 210 Mach-O image loaded");

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

    for (std::size_t step = 0; step < 15000; ++step) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(memory);
    }
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "Execution halted at WFI");

    // 1. Verify Header at 0x28000
    const std::uint32_t magic = memory.read_u32(0x28000ULL);
    const std::uint32_t total_pages = memory.read_u32(0x28004ULL);
    const std::uint32_t managed_pages = memory.read_u32(0x28008ULL);
    const std::uint32_t free_pages = memory.read_u32(0x2800CULL);
    const std::uint64_t avail_st = memory.read_u64(0x28010ULL);
    const std::uint64_t avail_en = memory.read_u64(0x28018ULL);

    TEST_CHECK(magic == 0x584E5550u, "Header magic is 'XNUP' (0x584E5550)");
    TEST_CHECK(total_pages == 512, "Total physical pages in 8 MiB is 512 (16KB granule)");
    TEST_CHECK(managed_pages == 496, "Managed pages is 496 (frames 16..511)");
    TEST_CHECK(free_pages == 496, "Initial free pages is 496");
    TEST_CHECK(avail_st == 0x40000ULL, "avail_start is reconciled to 0x40000 (Frame 16)");
    TEST_CHECK(avail_en == 0x800000ULL, "avail_end is 0x800000 (8 MiB boundary)");

    // 2. Verify Attributes Array at 0x28020
    // Reserved frames 0..15 must have attribute 0x00
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t attr = memory.read_u8(0x28020ULL + i);
        TEST_CHECK(attr == 0x00, "Frame " + std::to_string(i) + " is reserved (attr=0x00)");
    }
    // Managed frames 16..511 must have attribute 0x04 (XNU_PHYS_MANAGED)
    for (std::size_t i = 16; i < 512; ++i) {
        const std::uint8_t attr = memory.read_u8(0x28020ULL + i);
        TEST_CHECK(attr == 0x04, "Frame " + std::to_string(i) + " is managed (attr=0x04)");
    }

    // 3. Verify Page Descriptors at 0x28220
    // Each xnu_vm_page_t: phys_page(u32), wire_count(u16), state(u8), flags(u8) = 8 bytes
    for (std::size_t i = 0; i < 496; ++i) {
        const std::uint64_t desc_addr = 0x28220ULL + (i * 8);
        const std::uint32_t pfn = memory.read_u32(desc_addr);
        const std::uint16_t wire_count = memory.read_u16(desc_addr + 4);
        const std::uint8_t state = memory.read_u8(desc_addr + 6);
        const std::uint8_t flags = memory.read_u8(desc_addr + 7);

        TEST_CHECK(pfn == static_cast<std::uint32_t>(16 + i), "Page descriptor PFN correct");
        TEST_CHECK(wire_count == 0, "Initial wire_count is 0");
        TEST_CHECK(state == 0, "Initial page state is FREE (0)");
        TEST_CHECK(flags == 0, "Initial flags are 0");
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.5 Physical Page Metadata Tests ===\n";
    test_physical_page_metadata_and_free_list();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.5: All physical page metadata tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.5: " << g_failures << " tests failed.\n";
        return 1;
    }
}
