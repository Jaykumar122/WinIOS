#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "loader/kernel_image_parser.h"
#include "loader/kernel_image_mapper.h"
#include "device/kernel_boot_devicetree.h"
#include "firmware/kernel_boot_args_builder.h"
#include "cpu/arm64_cpu_boot_state.h"

class CPU;
class Memory;

namespace winios::device {
class AppleAIC;
}

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Kernel Boot Configuration
// -----------------------------------------------------------------------------
struct KernelBootConfig {
    std::vector<std::uint8_t> raw_kernel_image;
    std::string               command_line{"-v debug=0x144 keepsyms=1 serial=3"};
    std::uint64_t             dram_base{0x800000000ULL};        // Apple A16 DRAM base
    std::uint64_t             dram_size{0x180000000ULL};        // 6 GB
    std::uint32_t             num_cores{6};                     // 2 P-cores + 4 E-cores
    std::uint64_t             boot_flags{0ULL};
    std::uint64_t             custom_initial_sp{0ULL};          // If 0, auto-computed
    bool                      enable_apple_devicetree{true};
    bool                      enable_apple_aic{false};          // Campaign 206: Apple AIC2 responder
    std::uint64_t             aic_base{0x23B100000ULL};
    std::uint64_t             aic_size{0x00100000ULL};
    std::uint64_t             vbar_el1{0xFFFFFE0007002000ULL}; // 2KB aligned default
    std::uint64_t             sctlr_el1{0x30D00800ULL};
    std::uint64_t             tcr_el1{0x2B5593519ULL};          // 16KB granule, 48-bit VA
    std::uint64_t             ttbr0_el1{0x804010000ULL};
    std::uint64_t             ttbr1_el1{0x804020000ULL};
    std::uint64_t             mair_el1{0x0000000444FF0400ULL};
};

// -----------------------------------------------------------------------------
// Kernel Boot Result
// -----------------------------------------------------------------------------
struct KernelBootResult {
    bool          success{false};
    std::string   error_message;
    std::uint64_t entry_pc{0};
    std::uint64_t boot_args_phys{0};
    std::uint64_t device_tree_phys{0};
    std::uint32_t device_tree_size{0};
    std::uint64_t initial_sp{0};
    std::uint64_t top_of_kernel_data{0};
    std::size_t   segments_mapped{0};
    std::size_t   bytes_mapped{0};
    std::shared_ptr<device::AppleAIC> aic{nullptr};
};

// -----------------------------------------------------------------------------
// KernelBootCoordinator
// -----------------------------------------------------------------------------
class KernelBootCoordinator {
public:
    static KernelBootResult prepare_and_boot(
        CPU& cpu,
        ::Memory& memory,
        const KernelBootConfig& config);
};

} // namespace winios::runtime
