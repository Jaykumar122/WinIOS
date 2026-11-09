#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "device/apple_devicetree.h"
#include "memory/memory.h"
#include "runtime/apple_kernel_execution_engine.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleDeviceTreeStagingContract: Memory parameters for DeviceTree staging
// -----------------------------------------------------------------------------
struct AppleDeviceTreeStagingContract {
    std::uint64_t dtb_phys{0x48000000ULL};
    std::uint32_t dtb_len{0x4000};
    std::uint64_t ram_base{0x40000000ULL};
    std::uint64_t ram_size{6ULL * 1024 * 1024 * 1024};
    std::uint64_t kernel_phys_start{0x40000000ULL};
    std::uint64_t kernel_phys_top{0x41000000ULL};
    std::uint64_t boot_args_phys{0x43F80000ULL};
    std::uint32_t boot_args_size{736}; // Canonical Rev 2 default (736 bytes)
    std::uint64_t framebuffer_phys{0x50000000ULL};
    std::uint32_t framebuffer_size{1179 * 2556 * 4};
};

// -----------------------------------------------------------------------------
// AppleDeviceTreeConsumptionReport: Result of analyzing guest memory accesses
// -----------------------------------------------------------------------------
struct AppleDeviceTreeConsumptionReport {
    bool handoff_verified{false};
    bool recursive_traversal_verified{false};
    std::size_t total_dtb_reads{0};
    std::size_t header_reads{0};
    std::size_t property_reads{0};
    std::size_t total_nodes_discovered{0};
    std::size_t max_depth_reached{0};
    std::uint64_t min_read_address{0};
    std::uint64_t max_read_address{0};
    std::uint32_t root_nproperties_read{0};
    std::uint32_t root_nchildren_read{0};
    bool verified_within_bounds{false};
    std::vector<std::string> discovered_properties;
    std::vector<std::string> discovered_nodes;
    std::string diagnostic_summary;
};

// -----------------------------------------------------------------------------
// AppleDeviceTreeHandoffManager: Validates staging, structures, and guest consumption
// -----------------------------------------------------------------------------
class AppleDeviceTreeHandoffManager {
public:
    // 1. Validates physical memory bounds, alignment, non-overlap, and overflow
    [[nodiscard]] static bool validate_staging_contract(
        const AppleDeviceTreeStagingContract& contract,
        std::string* out_error = nullptr);

    // 2. Directly parses and validates staged DeviceTree in guest physical Memory
    [[nodiscard]] static bool validate_staged_memory(
        const Memory& mem,
        std::uint64_t dtb_phys,
        std::uint32_t dtb_len,
        std::string* out_error = nullptr);

    // 3. Validates required node hierarchy and properties for iPhone 15 (iPhone15,4 / D37AP / T8120)
    [[nodiscard]] static bool validate_required_iphone15_nodes(
        const AppleDeviceTreeNode& root,
        std::string* out_error = nullptr);

    // 4. Validates required node hierarchy and properties for iPhone 14 Pro (iPhone15,2 / D73AP / T8120)
    [[nodiscard]] static bool validate_required_iphone14_pro_nodes(
        const AppleDeviceTreeNode& root,
        std::string* out_error = nullptr);

    // 5. Analyzes guest CPU memory access trace to verify DTB consumption
    [[nodiscard]] static AppleDeviceTreeConsumptionReport analyze_guest_consumption(
        const std::vector<runtime::AppleMemoryAccessRecord>& trace,
        std::uint64_t dtb_phys,
        std::uint32_t dtb_len);

    // 6. Generates a synthetic Mach-O fileset kernel that traverses staged DeviceTree in RAM (Root Node)
    [[nodiscard]] static std::vector<std::uint8_t> create_synthetic_devicetree_traversal_kernel(
        std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL,
        std::uint64_t text_fileoff = 0x4000ULL);

    // 7. Generates a synthetic Mach-O fileset kernel that performs recursive DeviceTree traversal
    [[nodiscard]] static std::vector<std::uint8_t> create_recursive_devicetree_traversal_kernel(
        std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL,
        std::uint64_t text_fileoff = 0x4000ULL);
};

} // namespace winios::device
