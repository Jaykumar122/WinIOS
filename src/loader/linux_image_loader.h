#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "loader/linux_boot_protocol.h"
#include "memory/memory.h"

namespace winios::loader {

enum class LinuxImageLoadStatus {
    Success,
    FileNotFound,
    FileReadError,
    TruncatedHeader,
    InvalidMagic,
    BigEndianUnsupported,
    RamCapacityExceeded,
    MisalignedRamBase,
    MemoryWriteFailed
};

struct LinuxKernelLoadResult {
    LinuxImageLoadStatus status{LinuxImageLoadStatus::FileNotFound};
    std::string error_message;
    LinuxArm64ImageHeader header{};
    std::uint64_t load_address{0};
    std::uint64_t entry_point{0};
    std::uint64_t effective_size{0};
    std::size_t file_bytes_loaded{0};
    std::string sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == LinuxImageLoadStatus::Success;
    }
};

class LinuxImageLoader {
public:
    LinuxImageLoader() = default;
    ~LinuxImageLoader() = default;

    // Load from filesystem
    [[nodiscard]] static LinuxKernelLoadResult load_from_file(
        const std::string& path,
        Memory& guest_mem,
        std::uint64_t ram_base,
        std::uint64_t ram_size);

    // Load from memory buffer (useful for testing and embedded kernels)
    [[nodiscard]] static LinuxKernelLoadResult load_from_buffer(
        const void* data,
        std::size_t size,
        Memory& guest_mem,
        std::uint64_t ram_base,
        std::uint64_t ram_size);

    // SHA-256 calculation helper for kernel verification
    [[nodiscard]] static std::string compute_sha256(const void* data, std::size_t size);
};

} // namespace winios::loader
