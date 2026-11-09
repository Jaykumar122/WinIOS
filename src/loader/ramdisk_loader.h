#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "loader/guest_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// RamdiskFormat: Canonical filesystem and container format detection
// -----------------------------------------------------------------------------
enum class RamdiskFormat {
    Unknown,
    RawBinary,
    ApfsContainer, // Apple File System Container (magic 'NXSB' / 0x4253584E)
    HfsPlus,       // Apple Hierarchical File System Plus (magic 'H+' / 0x482B or 'HX' / 0x4858)
    Fat32          // FAT32 boot sector (0x55, 0xAA boot signature)
};

// -----------------------------------------------------------------------------
// RamdiskLoadStatus: Result code for ramdisk placement and registration
// -----------------------------------------------------------------------------
enum class RamdiskLoadStatus {
    Success,
    FileNotFound,
    FileReadError,
    EmptyBuffer,
    MisalignedPlacement,
    RamCapacityExceeded,
    MemoryCollision,
    MemoryWriteFailed
};

// -----------------------------------------------------------------------------
// RamdiskLoadResult: Metadata describing loaded ramdisk in guest memory
// -----------------------------------------------------------------------------
struct RamdiskLoadResult {
    RamdiskLoadStatus status{RamdiskLoadStatus::FileNotFound};
    std::string error_message;
    RamdiskFormat format{RamdiskFormat::Unknown};
    std::uint64_t physical_address{0};
    std::size_t size{0};
    std::string sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == RamdiskLoadStatus::Success;
    }
};

// -----------------------------------------------------------------------------
// RamdiskLoader: Ingests, inspects, and maps Darwin / iOS root filesystems
// -----------------------------------------------------------------------------
class RamdiskLoader {
public:
    RamdiskLoader() = default;
    ~RamdiskLoader() = default;

    // Detect filesystem format from buffer header
    [[nodiscard]] static RamdiskFormat detect_format(const void* data, std::size_t size) noexcept;

    // Load from memory buffer with 16 KB page alignment for Apple Silicon
    [[nodiscard]] static RamdiskLoadResult load_from_buffer(
        const void* data,
        std::size_t size,
        Memory& guest_mem,
        std::uint64_t target_load_addr,
        std::uint64_t ram_base,
        std::uint64_t ram_size,
        GuestMemoryMap* memory_map = nullptr);

    // Load from file path
    [[nodiscard]] static RamdiskLoadResult load_from_file(
        const std::string& path,
        Memory& guest_mem,
        std::uint64_t target_load_addr,
        std::uint64_t ram_base,
        std::uint64_t ram_size,
        GuestMemoryMap* memory_map = nullptr);
};

} // namespace winios::loader
