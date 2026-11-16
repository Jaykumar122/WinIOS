#include "loader/kernel_image_mapper.h"

#include <algorithm>
#include <cstring>

namespace winios::loader {

void KernelImageMapper::rollback(
    Memory& memory,
    const std::vector<std::pair<std::uint64_t, std::size_t>>& mapped_ranges) {

    for (const auto& [addr, size] : mapped_ranges) {
        for (std::size_t i = 0; i < size; ++i) {
            std::uint64_t target = addr + i;
            if (target < memory.size()) {
                memory.write_u8(target, 0);
            }
        }
    }
}

KernelMappingResult KernelImageMapper::map_image(
    Memory& memory,
    const std::uint8_t* raw_image_data,
    std::size_t raw_image_size,
    const KernelParsedImage& parsed_image,
    std::uint64_t ram_base,
    std::uint64_t ram_size) {

    KernelMappingResult result;

    if (!raw_image_data || raw_image_size == 0) {
        result.error_message = "Null or empty raw image data";
        return result;
    }

    if (parsed_image.segments.empty()) {
        result.error_message = "Parsed image has zero segments";
        return result;
    }

    if (memory.size() == 0) {
        result.error_message = "Target memory size is zero";
        return result;
    }

    std::uint64_t lowest_va = parsed_image.preferred_base_vaddr;

    for (const auto& seg : parsed_image.segments) {
        if (seg.vmsize == 0) continue;

        if (seg.vmaddr < lowest_va) {
            result.error_message = "Segment vmaddr precedes lowest base vmaddr";
            rollback(memory, result.mapped_ranges);
            return result;
        }

        std::uint64_t rel_offset = seg.vmaddr - lowest_va;
        std::uint64_t phys_addr = ram_base + rel_offset;

        // Check RAM capacity bounds
        if (rel_offset + seg.vmsize > ram_size) {
            result.error_message = "Segment '" + seg.name + "' exceeds RAM capacity";
            rollback(memory, result.mapped_ranges);
            return result;
        }

        // Check target memory bounds
        // In the virtual platform, memory can be mapped at [0, memory.size()) or [ram_base, ram_base + memory.size())
        std::uint64_t mem_offset = (phys_addr >= ram_base && phys_addr < ram_base + memory.size())
                                       ? (phys_addr - ram_base)
                                       : phys_addr;

        if (mem_offset + seg.vmsize > memory.size()) {
            result.error_message = "Segment '" + seg.name + "' exceeds target memory buffer size";
            rollback(memory, result.mapped_ranges);
            return result;
        }

        // Validate file offsets
        if (seg.filesize > 0) {
            if (seg.fileoff + seg.filesize > raw_image_size) {
                result.error_message = "Segment '" + seg.name + "' file offset exceeds raw image size";
                rollback(memory, result.mapped_ranges);
                return result;
            }

            // Copy initialized data
            for (std::size_t i = 0; i < seg.filesize; ++i) {
                memory.write_u8(mem_offset + i, raw_image_data[seg.fileoff + i]);
            }
        }

        // Zero-fill remaining BSS space (vmsize > filesize)
        if (seg.vmsize > seg.filesize) {
            std::size_t zero_start = static_cast<std::size_t>(mem_offset + seg.filesize);
            std::size_t zero_count = static_cast<std::size_t>(seg.vmsize - seg.filesize);
            for (std::size_t i = 0; i < zero_count; ++i) {
                memory.write_u8(zero_start + i, 0);
            }
        }

        result.mapped_ranges.push_back({mem_offset, static_cast<std::size_t>(seg.vmsize)});
        result.bytes_mapped += static_cast<std::size_t>(seg.vmsize);
        result.segments_mapped++;
    }

    result.success = true;
    return result;
}

} // namespace winios::loader
