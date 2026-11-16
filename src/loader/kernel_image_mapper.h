#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "memory/memory.h"
#include "loader/kernel_image_parser.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Mapping Result with Transactional Rollback Tracking
// -----------------------------------------------------------------------------
struct KernelMappingResult {
    bool        success{false};
    std::size_t bytes_mapped{0};
    std::size_t segments_mapped{0};
    std::string error_message;
    std::vector<std::pair<std::uint64_t, std::size_t>> mapped_ranges;
};

// -----------------------------------------------------------------------------
// KernelImageMapper
// -----------------------------------------------------------------------------
class KernelImageMapper {
public:
    static KernelMappingResult map_image(
        Memory& memory,
        const std::uint8_t* raw_image_data,
        std::size_t raw_image_size,
        const KernelParsedImage& parsed_image,
        std::uint64_t ram_base,
        std::uint64_t ram_size);

    static void rollback(
        Memory& memory,
        const std::vector<std::pair<std::uint64_t, std::size_t>>& mapped_ranges);
};

} // namespace winios::loader
