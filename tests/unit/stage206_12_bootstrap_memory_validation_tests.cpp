#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "device/kernel_boot_devicetree.h"
#include "firmware/kernel_boot_args_builder.h"
#include "memory/boot_memory_descriptor.h"
#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

using namespace winios;
using namespace winios::device;
using namespace winios::firmware;
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

void test_bootstrap_memory_parameters() {
    std::cout << "[Test 1] Kernel Bootstrap Memory Parameters Validation...\n";

    constexpr std::uint64_t kPhysBase = 0x800000000ULL;
    constexpr std::uint64_t kMemSize  = 0x180000000ULL; // 6 GB DRAM
    constexpr std::uint64_t kGranule  = 0x4000ULL;      // 16 KB

    // 1. Physical Base Alignment
    TEST_CHECK((kPhysBase % kGranule) == 0, "Physical base must be 16KB aligned");
    TEST_CHECK(kPhysBase >= 0x80000000ULL, "Physical base in Apple Silicon DRAM range");

    // 2. Memory Size Granularity
    TEST_CHECK((kMemSize % kGranule) == 0, "DRAM capacity must be 16KB multiple");
    TEST_CHECK(kMemSize >= 0x40000000ULL, "DRAM capacity at least 1 GB");

    // 3. Apple BootArgs Rev 2 Field Layout
    KernelBootArgsBuilder builder;
    builder.set_virt_base(0xFFFFFE0007004000ULL)
           .set_phys_base(kPhysBase)
           .set_mem_size(kMemSize)
           .set_top_of_kernel_data(kPhysBase + 0x08000000ULL)
           .set_device_tree(kPhysBase + 0x04000000ULL, 0x10000)
           .set_command_line("-v keepsyms=1");

    auto raw_args = builder.serialize();
    TEST_CHECK(raw_args.size() == 736, "Apple BootArgs Rev 2 size must be exactly 736 bytes");

    // Inspect byte offsets
    std::uint64_t virt_base_rd = 0;
    std::uint64_t phys_base_rd = 0;
    std::uint64_t mem_size_rd  = 0;
    std::uint64_t top_data_rd  = 0;
    std::uint64_t dt_paddr_rd  = 0;
    std::uint32_t dt_len_rd    = 0;

    std::memcpy(&virt_base_rd, raw_args.data() + 8, 8);
    std::memcpy(&phys_base_rd, raw_args.data() + 16, 8);
    std::memcpy(&mem_size_rd,  raw_args.data() + 24, 8);
    std::memcpy(&top_data_rd,  raw_args.data() + 32, 8);
    std::memcpy(&dt_paddr_rd,  raw_args.data() + 96, 8);
    std::memcpy(&dt_len_rd,    raw_args.data() + 104, 4);

    TEST_CHECK(virt_base_rd == 0xFFFFFE0007004000ULL, "virtBase at offset 8 matches");
    TEST_CHECK(phys_base_rd == kPhysBase, "physBase at offset 16 matches");
    TEST_CHECK(mem_size_rd  == kMemSize,  "memSize at offset 24 matches");
    TEST_CHECK(top_data_rd  == (kPhysBase + 0x08000000ULL), "topOfKernelData at offset 32 matches");
    TEST_CHECK(dt_paddr_rd  == (kPhysBase + 0x04000000ULL), "deviceTreeP at offset 96 matches");
    TEST_CHECK(dt_len_rd    == 0x10000, "deviceTreeLength at offset 104 matches");

    std::cout << "  -> Bootstrap memory parameters verified successfully.\n";
}

void test_page_table_reference_boundaries() {
    std::cout << "[Test 2] Page Table Reference Boundaries & Memory Stealer...\n";

    Memory memory(0x400000); // 4 MB

    EarlyPhysicalPageAllocator allocator(0x100000ULL, 0x300000ULL);
    TEST_CHECK(allocator.current_watermark() == 0x100000ULL, "Initial watermark matches base");
    TEST_CHECK(allocator.remaining_bytes() == 0x200000ULL, "Initial capacity is 2 MB");

    // Allocate 1 page for L1 translation table root
    const std::uint64_t l1_page = allocator.allocate_pages(memory, 1);
    TEST_CHECK(l1_page == 0x100000ULL, "L1 page allocated at base");
    TEST_CHECK((l1_page % 0x4000ULL) == 0, "L1 page is 16KB aligned");
    TEST_CHECK(allocator.current_watermark() == 0x104000ULL, "Watermark advanced by 16KB");

    // Allocate 2 pages for L2 tables
    const std::uint64_t l2_pages = allocator.allocate_pages(memory, 2);
    TEST_CHECK(l2_pages == 0x104000ULL, "L2 pages allocated contiguous");
    TEST_CHECK(allocator.current_watermark() == 0x10C000ULL, "Watermark advanced by 32KB");

    // Verify zero fill
    for (std::size_t i = 0; i < 0x4000; ++i) {
        if (memory.read_u8(static_cast<std::size_t>(l1_page + i)) != 0) {
            TEST_CHECK(false, "Allocated translation page must be zero filled");
            break;
        }
    }

    std::cout << "  -> Page table reference boundaries verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.12 Kernel Bootstrap Memory Validation Tests ===\n";
    test_bootstrap_memory_parameters();
    test_page_table_reference_boundaries();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.12 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.12 Tests FAILED ===\n";
        return 1;
    }
}
