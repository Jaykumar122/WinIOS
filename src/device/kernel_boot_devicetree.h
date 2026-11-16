#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// Kernel Boot Device Tree Configuration
// -----------------------------------------------------------------------------
struct KernelBootDeviceTreeConfig {
    std::string   boot_args{"-v serial=3"};
    std::uint64_t ram_base{0x800000000ULL};
    std::uint64_t ram_size{0x40000000ULL}; // 1 GB
    std::uint32_t num_cores{6};            // iPhone 15: 2 P + 4 E
    std::uint32_t p_cores{2};
    std::uint32_t e_cores{4};
    std::uint64_t timer_frequency{24000000ULL}; // 24 MHz
    std::uint64_t uart_base{0x00090000ULL};
    std::uint64_t aic_base{0x23B100000ULL};     // Apple AIC2 MMIO base on T8120
    std::uint64_t aic_size{0x00100000ULL};     // 1 MB aperture
    std::uint64_t gic_dist_base{0x08000000ULL};
    std::uint64_t gic_redist_base{0x080A0000ULL};
    std::string   compatible{"apple,d37ap"};
    std::string   soc_name{"T8120"};
};

// -----------------------------------------------------------------------------
// KernelBootDeviceTreeBuilder
// -----------------------------------------------------------------------------
class KernelBootDeviceTreeBuilder {
public:
    static std::vector<std::uint8_t> build_apple_devicetree(const KernelBootDeviceTreeConfig& config);
    static std::vector<std::uint8_t> build_fdt(const KernelBootDeviceTreeConfig& config);
    static bool validate_apple_devicetree(const std::uint8_t* data, std::size_t size, std::string* out_error = nullptr);
};

} // namespace winios::device
