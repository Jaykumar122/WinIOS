#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/virtual_page_table.h"
#include "loader/xnu_genuine_init_builder.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::memory;
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

void test_virtual_page_table_mapping() {
    std::cout << "[Test 1] Stage 210.8: VirtualPageTable Mapping & Translation Walk...\n";

    Memory mem(0x800000); // 8 MiB guest RAM
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    VirtualPageTable vpt(mem);

    const std::uint64_t root_l2_table = 0x24000ULL;

    // Map kernel text page: VA 0x10000 -> PA 0x10000 (KernelExecute)
    TEST_CHECK(vpt.map_page(root_l2_table, 0x10000ULL, 0x10000ULL,
                            PagePermissions::KernelExecute, PageMemoryType::NormalMemory, &ppm),
               "Map VA 0x10000 KernelExecute");

    // Translate VA 0x10000
    PageDescriptorFields fields{};
    const std::uint64_t pa = vpt.translate(root_l2_table, 0x10000ULL, &fields);
    TEST_CHECK(pa == 0x10000ULL, "Translated PA matches 0x10000");
    TEST_CHECK(fields.valid, "Page descriptor is valid");
    TEST_CHECK(fields.privileged_execute, "Page has privileged_execute permission");
    TEST_CHECK(!fields.writable, "Page is read-only / execute");

    // Map Device MMIO: VA 0x23B100000 -> PA 0x23B100000 (DeviceMemory)
    TEST_CHECK(vpt.map_page(root_l2_table, 0x23B100000ULL, 0x23B100000ULL,
                            PagePermissions::KernelReadWrite, PageMemoryType::DeviceMemory, &ppm),
               "Map AIC2 MMIO aperture");

    PageDescriptorFields dev_fields{};
    const std::uint64_t dev_pa = vpt.translate(root_l2_table, 0x23B100000ULL, &dev_fields);
    TEST_CHECK(dev_pa == 0x23B100000ULL, "AIC2 MMIO translates to 0x23B100000");
    TEST_CHECK(dev_fields.attr_index == 1, "Device memory uses AttrIndx 1");

    // Unmap page
    TEST_CHECK(vpt.unmap_page(root_l2_table, 0x10000ULL), "Unmap VA 0x10000");
    const std::uint64_t unmapped_pa = vpt.translate(root_l2_table, 0x10000ULL);
    TEST_CHECK(unmapped_pa == 0ULL, "Unmapped VA translates to 0");
}

void test_guest_kernel_map_structure() {
    std::cout << "[Test 2] Stage 210.8: In-Memory Darwin kernel_map Header Verification...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x800000);

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    KernelBootCoordinator::prepare_and_boot(cpu, mem, config);
    for (std::size_t i = 0; i < 15000; ++i) {
        if (cpu.is_waiting_for_interrupt()) break;
        cpu.step(mem);
    }

    // Inspect guest kernel_map header at 0x29200
    const std::uint32_t magic = mem.read_u32(0x29200ULL);
    const std::uint32_t entry_count = mem.read_u32(0x29204ULL);
    const std::uint64_t start_va = mem.read_u64(0x29208ULL);
    const std::uint64_t end_va = mem.read_u64(0x29210ULL);
    const std::uint64_t size = mem.read_u64(0x29218ULL);

    TEST_CHECK(magic == 0x4B4D4150u, "kernel_map magic matches 'KMAP'");
    TEST_CHECK(entry_count == 5, "kernel_map has 5 initial entries");
    TEST_CHECK(start_va == 0x0ULL, "kernel_map starts at 0x0");
    TEST_CHECK(end_va == 0x800000ULL, "kernel_map ends at 0x800000");
    TEST_CHECK(size == 0x800000ULL, "kernel_map size is 8 MiB");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.8 Kernel VA Map Tests ===\n";
    test_virtual_page_table_mapping();
    test_guest_kernel_map_structure();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.8: All kernel VA map tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.8: " << g_failures << " tests failed.\n";
        return 1;
    }
}
