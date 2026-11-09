#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Shared Library Mapper Status Codes
// -----------------------------------------------------------------------------
enum class SharedLibMapStatus {
    Success,
    AddressCollision,
    MemoryExhausted,
    InvalidMachO,
    MemoryWriteFailed,
    ImageNotFound,
    UnalignedSegment
};

[[nodiscard]] const char* shared_lib_map_status_string(SharedLibMapStatus s) noexcept;

// -----------------------------------------------------------------------------
// Mapped Image Record
// -----------------------------------------------------------------------------
struct MappedImageRecord {
    std::string                          path;
    std::string                          install_name;
    std::uint64_t                        base_vaddr{0};
    std::uint64_t                        vmsize{0};
    std::uint64_t                        aslr_slide{0};
    bool                                 is_main_executable{false};
    std::vector<UserspaceSegment>        segments;
    std::map<std::string, std::uint64_t> exports;
};

// -----------------------------------------------------------------------------
// SharedLibraryMapper: Multi-Image Address Space Allocator and Mapper
// -----------------------------------------------------------------------------
class SharedLibraryMapper {
public:
    explicit SharedLibraryMapper(
        std::uint64_t dylib_arena_base = 0x0000000180000000ULL,
        std::uint64_t dylib_arena_size = 0x0000000080000000ULL);
    ~SharedLibraryMapper() = default;

    // Map the primary executable into guest memory
    SharedLibMapStatus map_main_executable(
        const void* macho_bytes,
        std::size_t macho_size,
        Memory& memory,
        std::uint64_t aslr_slide,
        std::string& out_error);

    // Map a shared library (dylib) into guest memory at an automatically or explicitly allocated address
    SharedLibMapStatus map_dylib(
        const void* macho_bytes,
        std::size_t macho_size,
        const std::string& path,
        Memory& memory,
        std::optional<std::uint64_t> requested_base,
        std::uint64_t aslr_slide,
        std::string& out_error);

    // Unmap a previously mapped image, reclaiming address space and zeroing memory
    SharedLibMapStatus unmap_image(
        std::string_view path,
        Memory& memory,
        std::string& out_error);

    // Query whether an address range collides with any mapped segment
    [[nodiscard]] bool check_collision(
        std::uint64_t vaddr,
        std::uint64_t size) const noexcept;

    // Lookup mapped image by path or install name
    [[nodiscard]] const MappedImageRecord* find_image(std::string_view path) const noexcept;

    // Lookup image containing virtual address
    [[nodiscard]] const MappedImageRecord* find_image_at(std::uint64_t vaddr) const noexcept;

    // Lookup segment containing virtual address
    [[nodiscard]] const UserspaceSegment* find_segment_at(std::uint64_t vaddr) const noexcept;

    // Get all mapped images
    [[nodiscard]] const std::vector<MappedImageRecord>& mapped_images() const noexcept {
        return mapped_images_;
    }

    // Number of mapped images
    [[nodiscard]] std::size_t mapped_count() const noexcept {
        return mapped_images_.size();
    }

private:
    std::uint64_t                  dylib_arena_base_;
    std::uint64_t                  dylib_arena_size_;
    std::uint64_t                  next_dylib_alloc_vaddr_;
    std::vector<MappedImageRecord> mapped_images_;

    std::uint64_t allocate_dylib_vaddr(std::uint64_t required_size);
};

} // namespace winios::loader
