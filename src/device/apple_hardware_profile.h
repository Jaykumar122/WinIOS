#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleCpuCoreProfile: Architectural topology specification for an SoC core
// -----------------------------------------------------------------------------
struct AppleCpuCoreProfile {
    std::string name;             // e.g. "cpu0"
    std::string microarch;        // "Everest" (Performance) or "Sawtooth" (Efficiency)
    bool is_performance{false};
    std::uint64_t clock_frequency_hz{0};
    std::uint32_t core_id{0};
};

// -----------------------------------------------------------------------------
// ApplePeripheralMapping: Verified physical MMIO aperture and IRQ assignment
// -----------------------------------------------------------------------------
struct ApplePeripheralMapping {
    std::string name;
    std::string device_tree_path;
    std::uint64_t base_address{0};
    std::uint64_t size{0};
    std::uint32_t irq_line{0};
    bool has_irq{false};
};

// -----------------------------------------------------------------------------
// AppleDisplayGeometry: Super Retina XDR display panel dimensions
// -----------------------------------------------------------------------------
struct AppleDisplayGeometry {
    std::uint32_t width{1179};
    std::uint32_t height{2556};
    std::uint32_t depth{32};
    std::uint32_t row_bytes{1179 * 4}; // 4716 bytes
    std::uint64_t default_framebuffer_base{0x4F000000ULL};
};

// -----------------------------------------------------------------------------
// AppleHardwareProfile: Canonical verified specification for Apple Silicon target
// -----------------------------------------------------------------------------
struct AppleHardwareProfile {
    std::string model;
    std::string target_type;
    std::uint32_t board_id{0};
    std::uint32_t chip_id{0};
    std::uint32_t timebase_frequency_hz{24000000}; // 24 MHz
    std::uint32_t page_size_bytes{16384};          // 16 KB
    std::uint32_t page_shift{14};                  // 16 KB = 2^14
    std::uint32_t virtual_address_bits{36};        // 64 GB space (T0SZ=28, T1SZ=28)
    std::uint64_t standard_ram_size{6ULL * 1024 * 1024 * 1024}; // 6 GB LPDDR5
    std::uint64_t minimum_ram_size{256ULL * 1024 * 1024};       // 256 MB VM baseline

    std::vector<AppleCpuCoreProfile> cores;
    std::vector<ApplePeripheralMapping> peripherals;
    AppleDisplayGeometry display;

    // Factory method for iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120)
    [[nodiscard]] static AppleHardwareProfile get_iphone15_profile();

    // Factory method for iPhone 14 Pro (iPhone15,2 / D73AP / Apple A16 Bionic T8120)
    [[nodiscard]] static AppleHardwareProfile get_iphone14_pro_profile();

    // Verification methods
    [[nodiscard]] bool validate_devicetree(
        const AppleDeviceTreeNode& root,
        std::string& out_error) const;

    [[nodiscard]] bool validate_memory_layout(
        std::uint64_t ram_base,
        std::size_t ram_size,
        std::string& out_error) const;

    [[nodiscard]] std::string dump_profile_summary() const;
};

} // namespace winios::device
