#pragma once

#include "snapshot/snapshot_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace winios::snapshot {

inline constexpr std::array<std::uint8_t, 8> kWSnapMagic = {
    'W', 'S', 'N', 'A', 'P', '\0', '\1', '\0'
};

inline constexpr std::uint16_t kWSnapVersion1 = 1;
inline constexpr std::uint16_t kWSnapVersion2 = 2;
inline constexpr std::uint16_t kWSnapCurrentVersion = 1;
inline constexpr std::size_t kWSnapHeaderSize = 64;
inline constexpr std::size_t kWSnapSectionDescriptorSize = 40;

struct WSnapHeader {
    std::array<std::uint8_t, 8> magic{kWSnapMagic};
    std::uint16_t version{kWSnapCurrentVersion};
    std::uint16_t core_count{1};
    SnapshotCompressionType compression_type{SnapshotCompressionType::None};
    std::uint16_t flags{0};
    std::uint64_t timestamp_ns{0};
    std::uint64_t memory_capacity{0};
    std::uint64_t section_dir_offset{0};
    std::uint32_t section_count{0};
    std::uint32_t total_file_size{0};
    std::uint32_t reserved0{0};
    std::uint32_t reserved1{0};
    std::uint32_t reserved2{0};
    std::uint32_t header_crc32c{0};
};

struct WSnapSectionDescriptor {
    SnapshotSectionType section_type{SnapshotSectionType::Invalid};
    std::uint32_t section_id{0};       // Sub-identifier (e.g. core ID for per-core sections)
    std::uint64_t file_offset{0};      // File offset of section payload
    std::uint64_t compressed_size{0};  // Payload size in file
    std::uint64_t uncompressed_size{0};// Uncompressed payload size
    std::uint32_t section_crc32c{0};   // CRC32C of payload bytes
    std::uint16_t flags{0};            // Section-specific flags
    std::uint16_t reserved{0};         // Alignment padding
};

// Binary serialization / deserialization routines with explicit endianness
void encode_wsnap_header(const WSnapHeader& header, std::uint8_t* dst) noexcept;
[[nodiscard]] SnapshotResult decode_wsnap_header(const std::uint8_t* src, std::size_t size, WSnapHeader& out_header) noexcept;

void encode_section_descriptor(const WSnapSectionDescriptor& desc, std::uint8_t* dst) noexcept;
[[nodiscard]] SnapshotResult decode_section_descriptor(const std::uint8_t* src, std::size_t size, WSnapSectionDescriptor& out_desc) noexcept;

} // namespace winios::snapshot
