#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "device/fdt_builder.h"
#include "loader/guest_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

enum class InitramfsFormat {
    Unknown,
    CpioNewc, // "070701" - Standard Linux initramfs new ASCII format
    CpioCrc,  // "070702" - Linux initramfs new ASCII format with CRC
    CpioOld,  // "070707" - Legacy ASCII format
    Gzip,     // 0x1F, 0x8B - Compressed archive
    RawBinary // Raw filesystem image or uncompressed binary
};

enum class InitramfsLoadStatus {
    Success,
    FileNotFound,
    FileReadError,
    EmptyBuffer,
    MisalignedPlacement,
    RamCapacityExceeded,
    MemoryCollision,
    MemoryWriteFailed
};

struct InitramfsLoadResult {
    InitramfsLoadStatus status{InitramfsLoadStatus::FileNotFound};
    std::string error_message;
    InitramfsFormat format{InitramfsFormat::Unknown};
    std::uint64_t load_address{0}; // linux,initrd-start
    std::uint64_t end_address{0};  // linux,initrd-end
    std::size_t size{0};
    std::string sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == InitramfsLoadStatus::Success;
    }
};

class InitramfsLoader {
public:
    InitramfsLoader() = default;
    ~InitramfsLoader() = default;

    // Detect format from buffer header
    [[nodiscard]] static InitramfsFormat detect_format(const void* data, std::size_t size) noexcept;

    // Load from memory buffer
    [[nodiscard]] static InitramfsLoadResult load_from_buffer(
        const void* data,
        std::size_t size,
        Memory& guest_mem,
        std::uint64_t target_load_addr,
        std::uint64_t ram_base,
        std::uint64_t ram_size,
        GuestMemoryMap* memory_map = nullptr);

    // Load from file path
    [[nodiscard]] static InitramfsLoadResult load_from_file(
        const std::string& path,
        Memory& guest_mem,
        std::uint64_t target_load_addr,
        std::uint64_t ram_base,
        std::uint64_t ram_size,
        GuestMemoryMap* memory_map = nullptr);
};

} // namespace winios::loader
