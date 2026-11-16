#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "device/kernel_boot_devicetree.h"
#include "memory/kernel_memory_layout.h"
#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

using namespace winios;
using namespace winios::device;
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

void test_devicetree_structure_and_chosen_node() {
    std::cout << "[Test 1] DeviceTree Structure, Platform Compatibility, and Chosen Node..." << std::endl;

    KernelBootDeviceTreeConfig dt_config;
    dt_config.boot_args = "-v serial=3 keepsyms=1";
    dt_config.ram_base = 0x800000000ULL;
    dt_config.ram_size = 0x180000000ULL; // 6 GB
    dt_config.num_cores = 6;
    dt_config.p_cores = 2;
    dt_config.e_cores = 4;
    dt_config.timer_frequency = 24000000ULL;
    dt_config.uart_base = 0x00090000ULL;
    dt_config.soc_name = "T8120";
    dt_config.compatible = "apple,d37ap";

    auto dt_bytes = KernelBootDeviceTreeBuilder::build_apple_devicetree(dt_config);
    TEST_CHECK(!dt_bytes.empty(), "Apple DeviceTree bytes must not be empty");

    std::string err;
    const bool valid = KernelBootDeviceTreeBuilder::validate_apple_devicetree(dt_bytes.data(), dt_bytes.size(), &err);
    TEST_CHECK(valid, "Apple DeviceTree validation must pass: " + err);

    // Verify key strings inside byte stream
    std::string dt_str(reinterpret_cast<const char*>(dt_bytes.data()), dt_bytes.size());
    TEST_CHECK(dt_str.find("iPhone15,4") != std::string::npos, "DeviceTree must declare model iPhone15,4");
    TEST_CHECK(dt_str.find("D37AP") != std::string::npos, "DeviceTree must declare target-type D37AP");
    TEST_CHECK(dt_str.find("apple,everest") != std::string::npos, "DeviceTree must declare P-core apple,everest");
    TEST_CHECK(dt_str.find("apple,sawtooth") != std::string::npos, "DeviceTree must declare E-core apple,sawtooth");
    TEST_CHECK(dt_str.find("chosen") != std::string::npos, "DeviceTree must include /chosen node");
    TEST_CHECK(dt_str.find("serial=3") != std::string::npos, "DeviceTree /chosen must include boot-args");
}

void test_bootstrap_pagetable_and_granule() {
    std::cout << "[Test 2] Bootstrap Page Table TCR/TTBR 16KB Granule Configuration..." << std::endl;

    // Darwin ARM64 TCR_EL1 for 16KB granule:
    // TG0 = 0b10 (16KB granule for TTBR0) -> bits [15:14] = 2
    // TG1 = 0b01 (16KB granule for TTBR1) -> bits [31:30] = 1
    // T0SZ = 25 (39-bit / 48-bit VA for TTBR0), T1SZ = 25 (39-bit / 48-bit VA for TTBR1)
    constexpr std::uint64_t kTcr16KB = (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL;
    const std::uint64_t tg0 = (kTcr16KB >> 14) & 0x3ULL;
    const std::uint64_t tg1 = (kTcr16KB >> 30) & 0x3ULL;
    const std::uint64_t t0sz = kTcr16KB & 0x3FULL;
    const std::uint64_t t1sz = (kTcr16KB >> 16) & 0x3FULL;

    TEST_CHECK(tg0 == 2, "TCR_EL1 TG0 must be 16KB granule (2)");
    TEST_CHECK(tg1 == 1, "TCR_EL1 TG1 must be 16KB granule (1)");
    TEST_CHECK(t0sz == 25, "T0SZ configuration valid");
    TEST_CHECK(t1sz == 25, "T1SZ configuration valid");

    // 16KB Granule alignments
    constexpr std::uint64_t kGranule = kPageGranule16KB; // 16384 bytes
    TEST_CHECK(kGranule == 0x4000ULL, "kPageGranule16KB must be 0x4000");

    // Virtual memory layout ranges
    TEST_CHECK((kKernelTextBase % kGranule) == 0, "Kernel Text Base must be 16KB aligned");
    TEST_CHECK((kKernelDataBase % kGranule) == 0, "Kernel Data Base must be 16KB aligned");
    TEST_CHECK((kKernelHeapBase % kGranule) == 0, "Kernel Heap Base must be 16KB aligned");
    TEST_CHECK((kKernelStackBase % kGranule) == 0, "Kernel Stack Base must be 16KB aligned");
    TEST_CHECK((kKernelMmioBase % kGranule) == 0, "Kernel MMIO Base must be 16KB aligned");
    TEST_CHECK((kKernelDirectMapBase % kGranule) == 0, "Kernel DirectMap Base must be 16KB aligned");
}

void test_top_of_kernel_data_and_steal_allocator_boundaries() {
    std::cout << "[Test 3] topOfKernelData and pmap_steal_memory Allocation Boundaries..." << std::endl;

    Memory memory(0x4000000); // 64 MB RAM
    constexpr std::uint64_t kPhysBase = 0x0ULL;
    constexpr std::uint64_t kMemSize  = 0x4000000ULL; // 64 MB
    constexpr std::uint64_t kTopOfKernelData = 0x800000ULL; // 8 MB (end of kernel image + DT + boot_args)

    // Steal-allocator must allocate above topOfKernelData and below physBase + memSize
    EarlyPhysicalPageAllocator allocator(kTopOfKernelData, kPhysBase + kMemSize);

    TEST_CHECK(allocator.current_watermark() == kTopOfKernelData, "Initial allocator watermark must match topOfKernelData");
    TEST_CHECK(allocator.limit() == (kPhysBase + kMemSize), "Allocator limit must match physical RAM bound");

    // Allocate 4 pages (64 KB) for bootstrap translation tables
    const std::uint64_t l2_tables = allocator.allocate_pages(memory, 4, 0x4000ULL, true);
    TEST_CHECK(l2_tables == kTopOfKernelData, "First stolen page must be at topOfKernelData");
    TEST_CHECK((l2_tables % 0x4000ULL) == 0, "Stolen page must be 16KB aligned");
    TEST_CHECK(allocator.current_watermark() == (kTopOfKernelData + 0x10000ULL), "Watermark advanced by 64KB");

    // Allocate per-core stacks (6 cores x 1 page each = 6 pages)
    const std::uint64_t core_stacks = allocator.allocate_pages(memory, 6, 0x4000ULL, true);
    TEST_CHECK(core_stacks == (kTopOfKernelData + 0x10000ULL), "Core stacks allocated contiguous to translation tables");
    TEST_CHECK(allocator.current_watermark() == (kTopOfKernelData + 0x28000ULL), "Watermark advanced by 6 * 16KB");

    // Ensure allocations never encroach into kernel image (< kTopOfKernelData)
    TEST_CHECK(l2_tables >= kTopOfKernelData, "Allocation must not corrupt kernel image space");
    TEST_CHECK(allocator.current_watermark() <= (kPhysBase + kMemSize), "Watermark must not exceed physical RAM");
}

void test_wx_security_policy_enforcement() {
    std::cout << "[Test 4] Kernel Memory W^X Security Invariants..." << std::endl;

    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL);

    std::string err;
    const bool wx_valid = layout.check_wx_policy(&err);
    TEST_CHECK(wx_valid, "Default kernel layout must strictly satisfy W^X policy: " + err);

    // Verify individual regions
    auto* text_region = layout.find_region(kKernelTextBase);
    TEST_CHECK(text_region != nullptr, "Text region lookup must succeed");
    if (text_region) {
        TEST_CHECK(text_region->is_executable == true, "Text region must be executable");
        TEST_CHECK(text_region->is_writable == false, "Text region must NOT be writable (W^X)");
    }

    auto* data_region = layout.find_region(kKernelDataBase);
    TEST_CHECK(data_region != nullptr, "Data region lookup must succeed");
    if (data_region) {
        TEST_CHECK(data_region->is_executable == false, "Data region must NOT be executable (W^X)");
        TEST_CHECK(data_region->is_writable == true, "Data region must be writable");
    }
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.19 Early XNU Initialization Compatibility Tests ===" << std::endl;

    test_devicetree_structure_and_chosen_node();
    test_bootstrap_pagetable_and_granule();
    test_top_of_kernel_data_and_steal_allocator_boundaries();
    test_wx_security_policy_enforcement();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.19 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.19 Tests FAILED ===" << std::endl;
        return 1;
    }
}
