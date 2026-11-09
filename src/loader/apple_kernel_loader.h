#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/macho_loader.h"
#include "memory/apple_physical_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// AppleKernelLoadResult: Comprehensive result of kernelcache staging into guest RAM
// -----------------------------------------------------------------------------
struct AppleKernelLoadResult {
    bool success{false};
    std::string error_message;
    MachOLoadResult macho_result;
    std::uint64_t phys_load_base{0x40000000ULL};
    std::uint64_t virt_kernel_base{0xFFFFFE0007004000ULL};
    std::uint64_t phys_entry_point{0};
    std::uint64_t virt_entry_point{0};
    std::uint64_t top_of_kernel_data{0};
    std::uint64_t effective_kernel_size{0};
    std::size_t segment_count{0};
    std::size_t kext_count{0};
    std::string sha256_hex;
};

// -----------------------------------------------------------------------------
// AppleKernelLoader: Loads and validates genuine iOS 64-bit kernelcache images
// -----------------------------------------------------------------------------
class AppleKernelLoader {
public:
    AppleKernelLoader() = default;
    ~AppleKernelLoader() = default;

    // Load kernelcache from byte buffer into guest physical memory
    [[nodiscard]] static AppleKernelLoadResult load_kernelcache(
        const std::uint8_t* data,
        std::size_t size,
        Memory& guest_memory,
        const memory::ApplePhysicalMemoryMap& memory_map,
        std::uint64_t kaslr_slide = 0,
        const AdmissionPolicy& policy = AdmissionPolicy{});

    // Formatted inspection markdown report
    [[nodiscard]] static std::string generate_kernel_report(const AppleKernelLoadResult& result);
};

} // namespace winios::loader
