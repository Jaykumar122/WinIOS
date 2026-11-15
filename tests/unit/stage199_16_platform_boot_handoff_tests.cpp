// ============================================================================
// Stage 199.16: Platform Boot and Handoff Integration Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_devicetree.h"
#include "device/apple_devicetree_handoff.h"
#include "device/fdt_builder.h"
#include "device/virtual_platform.h"
#include "loader/synthetic_kernel_loader.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::loader;

// 1. Validate Synthetic Kernel Boot Handoff with DeviceTree & 16KB Page Granule
static bool test_synthetic_kernel_boot_handoff() {
    Memory mem(0x2000000); // 32 MiB
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t dram_base = 0x40000000ULL;
    const std::uint64_t dram_size = 0x02000000ULL; // 32 MiB
    const std::uint64_t boot_info_pa = 0x40001000ULL;
    const std::uint64_t dtb_pa = 0x40010000ULL;
    const std::uint64_t kernel_entry = 0x40100000ULL;
    const std::uint64_t sp_el1 = 0x40200000ULL;

    // Create Virtual Platform DTB
    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = 8;
    plat_cfg.ram_base = dram_base;
    plat_cfg.ram_size = dram_size;
    plat_cfg.has_iommu = true;
    plat_cfg.has_stage2 = true;
    auto dtb = FdtBuilder::build_platform_dtb(plat_cfg);
    TEST_ASSERT(!dtb.empty(), "Generated DTB must not be empty");

    // Write DTB into memory at offset relative to mem base (offset = dtb_pa - dram_base)
    const std::uint64_t dtb_offset = dtb_pa - dram_base;
    std::memcpy(mem.raw_data() + dtb_offset, dtb.data(), dtb.size());

    // Prepare SyntheticKernelBootInfo
    SyntheticKernelBootInfo info{};
    info.magic = kSyntheticKernelMagic;
    info.version = kSyntheticKernelVersion;
    info.flags = kBootFlagConsolePresent | kBootFlagTimerEnabled | kBootFlagDtbPresent;
    info.dram_base = dram_base;
    info.dram_size = dram_size;
    info.entry_point = kernel_entry;
    info.initial_sp_el1 = sp_el1;
    info.dtb_base = dtb_pa;
    info.dtb_size = dtb.size();
    info.cpu_count = 8;
    info.page_granule_bytes = 16384; // 16 KB A16 granule

    const std::uint64_t info_offset = boot_info_pa - dram_base;
    std::memcpy(mem.raw_data() + info_offset, &info, sizeof(info));

    // Verify deserialization from guest memory
    SyntheticKernelBootInfo read_back{};
    std::memcpy(&read_back, mem.raw_data() + info_offset, sizeof(info));

    TEST_ASSERT(read_back.magic == kSyntheticKernelMagic, "Magic must match");
    TEST_ASSERT(read_back.cpu_count == 8, "CPU count must be 8");
    TEST_ASSERT(read_back.page_granule_bytes == 16384, "Page granule must be 16KB");
    TEST_ASSERT(read_back.dtb_base == dtb_pa, "DTB base must match");

    // Initialize CPU for boot handoff:
    // Calling contract: X0 contains boot info physical address, PC at kernel_entry, SP_EL1 configured
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // SPSel = 1 (EL1h)
    cpu.write_register(0, boot_info_pa);
    cpu.write_register(1, dtb_pa);
    cpu.set_stack_pointer(sp_el1);
    cpu.set_sp_el1(sp_el1);
    cpu.set_program_counter(kernel_entry);

    TEST_ASSERT(cpu.get_current_el() == 1, "CPU must be at EL1 for kernel entry");
    TEST_ASSERT(cpu.read_register(0) == boot_info_pa, "X0 must point to boot info");
    TEST_ASSERT(cpu.read_register(1) == dtb_pa, "X1 must point to DTB");
    TEST_ASSERT(cpu.get_stack_pointer() == sp_el1, "SP must be at initial_sp_el1");
    TEST_ASSERT(cpu.get_program_counter() == kernel_entry, "PC must be at kernel entry");

    // Verify that DTB header at dtb_pa is valid FDT
    std::uint32_t fdt_magic = 0;
    std::memcpy(&fdt_magic, mem.raw_data() + dtb_offset, sizeof(fdt_magic));
    // FDT magic is big-endian 0xd00dfeed
    fdt_magic = ((fdt_magic & 0xFFu) << 24u) | (((fdt_magic >> 8u) & 0xFFu) << 16u) |
                (((fdt_magic >> 16u) & 0xFFu) << 8u) | ((fdt_magic >> 24u) & 0xFFu);
    TEST_ASSERT(fdt_magic == kFdtMagic, "FDT magic at DTB base must match 0xd00dfeed");

    std::cout << "[PASS] test_synthetic_kernel_boot_handoff\n";
    return true;
}

// 2. Validate Darwin Apple DeviceTree Staging Contract and Handoff
static bool test_darwin_apple_devicetree_staging_handoff() {
    AppleDeviceTreeStagingContract contract{};
    contract.ram_base = 0x40000000ULL;
    contract.ram_size = 0x40000000ULL; // 1 GiB for testing
    contract.dtb_phys = 0x48000000ULL;
    contract.dtb_len = 0x8000;
    contract.kernel_phys_start = 0x40000000ULL;
    contract.kernel_phys_top = 0x44000000ULL;
    contract.boot_args_phys = 0x47F00000ULL;
    contract.boot_args_size = 736;
    contract.framebuffer_phys = 0x50000000ULL;
    contract.framebuffer_size = 1179 * 2556 * 4;

    std::string err;
    TEST_ASSERT(AppleDeviceTreeHandoffManager::validate_staging_contract(contract, &err),
                ("Staging contract validation failed: " + err).c_str());

    // Generate Apple DeviceTree for iPhone 15 (6-core A16 Bionic topology)
    auto dtb_blob = AppleDeviceTreeBuilder::create_iphone15_virtual_platform_devicetree(
        contract.ram_base,
        contract.ram_size,
        "-v debug=0x144 serial=3",
        6,
        true,
        true
    );
    TEST_ASSERT(!dtb_blob.empty(), "Generated Apple DTB must not be empty");
    TEST_ASSERT(dtb_blob.size() <= contract.dtb_len, "DTB blob must fit within dtb_len");

    // Stage into guest Memory
    Memory mem(contract.ram_size);
    const std::uint64_t dtb_offset = contract.dtb_phys - contract.ram_base;
    std::memcpy(mem.raw_data() + dtb_offset, dtb_blob.data(), dtb_blob.size());

    // Validate staged memory directly using AppleDeviceTreeHandoffManager
    TEST_ASSERT(AppleDeviceTreeHandoffManager::validate_staged_memory(mem, dtb_offset, static_cast<std::uint32_t>(dtb_blob.size()), &err),
                ("validate_staged_memory failed: " + err).c_str());

    // Validate required iPhone 15 nodes in deserialized tree
    auto root = AppleDeviceTreeBuilder::deserialize(dtb_blob.data(), dtb_blob.size(), &err);
    TEST_ASSERT(root != nullptr, "Failed to deserialize staged Apple DTB");
    TEST_ASSERT(AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(*root, &err),
                ("validate_required_iphone15_nodes failed: " + err).c_str());

    std::cout << "[PASS] test_darwin_apple_devicetree_staging_handoff\n";
    return true;
}

int main() {
    std::cout << "--- Stage 199.16: Platform Boot and Handoff Integration Tests ---\n";
    if (!test_synthetic_kernel_boot_handoff()) return 1;
    if (!test_darwin_apple_devicetree_staging_handoff()) return 1;

    std::cout << "All Stage 199.16 tests passed successfully!\n";
    return 0;
}
