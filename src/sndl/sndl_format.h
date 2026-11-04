#pragma once

#include "sndl/sndl_types.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// WSndlHeader: Fixed 64-Byte File Header
// -----------------------------------------------------------------------------
struct alignas(8) WSndlHeader {
    std::array<std::uint8_t, 8> magic{kWSndlMagic};
    std::uint16_t version{kWSndlCurrentVersion};
    std::uint16_t core_count{1};
    std::uint32_t flags{0};
    std::uint64_t base_snapshot_hash{0};        // Low 64 bits of baseline .wsnap CRC/hash
    std::uint64_t creation_timestamp_ns{0};     // Host timestamp
    std::uint64_t total_record_count{0};        // Updated on clean finalization
    std::uint64_t total_instructions{0};        // Total instructions executed
    std::array<std::uint8_t, 12> snapshot_id{}; // UUID or snapshot tag
    std::uint32_t header_crc32c{0};             // CRC32C over bytes [0..59]
};
static_assert(sizeof(WSndlHeader) == 64, "WSndlHeader must be 64 bytes");

// -----------------------------------------------------------------------------
// WSndlBlockHeader: Fixed 48-Byte Block Header
// -----------------------------------------------------------------------------
struct alignas(8) WSndlBlockHeader {
    std::array<std::uint8_t, 4> magic{kWSndlBlockMagic};
    std::uint16_t version{1};
    std::uint16_t flags{0};
    std::uint32_t record_count{0};              // Number of records in this block
    std::uint32_t payload_bytes{0};             // Byte length of records payload
    std::uint64_t first_sequence{0};            // First sequence number in block
    std::uint64_t last_sequence{0};             // Last sequence number in block
    std::uint32_t payload_crc32c{0};            // CRC32C over block payload
    std::array<std::uint8_t, 8> reserved{};     // Alignment padding (zero-filled)
    std::uint32_t header_crc32c{0};             // CRC32C over bytes [0..43]
};
static_assert(sizeof(WSndlBlockHeader) == 48, "WSndlBlockHeader must be 48 bytes");

// -----------------------------------------------------------------------------
// WSndlFooter: Fixed 64-Byte Integrity Footer
// -----------------------------------------------------------------------------
struct alignas(8) WSndlFooter {
    std::array<std::uint8_t, 8> magic{kWSndlFooterMagic};
    std::uint16_t version{1};
    SndlFinalizationStatus finalization_status{SndlFinalizationStatus::Incomplete};
    std::uint32_t block_count{0};               // Total blocks written
    std::uint64_t total_records{0};             // Total records written
    std::uint64_t last_sequence{0};             // Highest sequence number written
    std::uint64_t total_instructions{0};        // Total guest instructions
    std::array<std::uint8_t, 20> reserved{};    // Alignment padding (zero-filled)
    std::uint32_t footer_crc32c{0};             // CRC32C over bytes [0..59]
};
static_assert(sizeof(WSndlFooter) == 64, "WSndlFooter must be 64 bytes");

// -----------------------------------------------------------------------------
// Format Encoding & Decoding Routines
// -----------------------------------------------------------------------------
void encode_sndl_header(const WSndlHeader& hdr, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlResult decode_sndl_header(const std::uint8_t* src, std::size_t size, WSndlHeader& out_hdr) noexcept;

void encode_sndl_block_header(const WSndlBlockHeader& blk, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlResult decode_sndl_block_header(const std::uint8_t* src, std::size_t size, WSndlBlockHeader& out_blk) noexcept;

void encode_sndl_footer(const WSndlFooter& ftr, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlResult decode_sndl_footer(const std::uint8_t* src, std::size_t size, WSndlFooter& out_ftr) noexcept;

} // namespace winios::sndl
