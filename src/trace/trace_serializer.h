#pragma once

#include "trace_event.h"
#include "trace_types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace winios::trace {

// -----------------------------------------------------------------------------
// Format Constants
// -----------------------------------------------------------------------------
constexpr std::uint64_t TRACE_FILE_MAGIC = 0x5254534F494E4957ULL;   // ASCII "WINIOSTR"
constexpr std::uint32_t TRACE_BLOCK_MAGIC = 0x4B4C4254u;           // ASCII "TBLK"
constexpr std::uint64_t TRACE_FOOTER_MAGIC = 0x5254464F49534957ULL;  // ASCII "WINIOFTR"

constexpr std::uint16_t TRACE_FILE_FORMAT_VERSION = 1;
constexpr std::uint16_t TRACE_BLOCK_VERSION       = 1;
constexpr std::uint16_t TRACE_FOOTER_VERSION      = 1;

constexpr std::size_t TRACE_EVENT_DISK_SIZE       = 64;
constexpr std::size_t TRACE_FILE_HEADER_SIZE      = 64;
constexpr std::size_t TRACE_BLOCK_HEADER_SIZE     = 32;
constexpr std::size_t TRACE_FILE_FOOTER_SIZE      = 64;

constexpr std::uint8_t TRACE_ENDIANNESS_LITTLE    = 1;
constexpr std::uint8_t TRACE_ENDIANNESS_BIG       = 2;

constexpr std::uint32_t TRACE_STATUS_CLEANLY_FINALIZED = 1;

// Maximum sensible limits to prevent unbounded allocation or integer overflow
constexpr std::uint32_t MAX_EVENTS_PER_BLOCK = 4096;
constexpr std::uint64_t MAX_TRACE_FILE_SIZE  = 16ULL * 1024 * 1024 * 1024; // 16 GiB

// -----------------------------------------------------------------------------
// Error Codes
// -----------------------------------------------------------------------------
enum class TraceFileError : std::uint32_t {
    Success                 = 0,
    InvalidPath             = 1,
    FileAlreadyExists       = 2,
    FileOpenFailed          = 3,
    FileReadFailed          = 4,
    FileWriteFailed         = 5,
    FileFlushFailed         = 6,
    FileCloseFailed         = 7,
    InvalidMagic            = 8,
    UnsupportedVersion      = 9,
    UnsupportedSchema       = 10,
    InvalidRecordSize       = 11,
    InvalidHeaderSize       = 12,
    InvalidEndianness       = 13,
    InvalidReservedField    = 14,
    InvalidEventType        = 15,
    InvalidPrivilegeLevel   = 16,
    InvalidAddressSpace     = 17,
    InvalidEventCount       = 18,
    InvalidBlockLength      = 19,
    ChecksumMismatch        = 20,
    TruncatedFile           = 21,
    MissingFooter           = 22,
    ArithmeticOverflow      = 23,
    BufferTooSmall          = 24,
    AlreadyOpen             = 25,
    NotOpen                 = 26
};

constexpr inline std::string_view to_string(TraceFileError err) noexcept {
    switch (err) {
        case TraceFileError::Success:               return "Success";
        case TraceFileError::InvalidPath:           return "InvalidPath";
        case TraceFileError::FileAlreadyExists:     return "FileAlreadyExists";
        case TraceFileError::FileOpenFailed:        return "FileOpenFailed";
        case TraceFileError::FileReadFailed:        return "FileReadFailed";
        case TraceFileError::FileWriteFailed:       return "FileWriteFailed";
        case TraceFileError::FileFlushFailed:       return "FileFlushFailed";
        case TraceFileError::FileCloseFailed:       return "FileCloseFailed";
        case TraceFileError::InvalidMagic:          return "InvalidMagic";
        case TraceFileError::UnsupportedVersion:    return "UnsupportedVersion";
        case TraceFileError::UnsupportedSchema:     return "UnsupportedSchema";
        case TraceFileError::InvalidRecordSize:     return "InvalidRecordSize";
        case TraceFileError::InvalidHeaderSize:     return "InvalidHeaderSize";
        case TraceFileError::InvalidEndianness:     return "InvalidEndianness";
        case TraceFileError::InvalidReservedField:  return "InvalidReservedField";
        case TraceFileError::InvalidEventType:      return "InvalidEventType";
        case TraceFileError::InvalidPrivilegeLevel: return "InvalidPrivilegeLevel";
        case TraceFileError::InvalidAddressSpace:   return "InvalidAddressSpace";
        case TraceFileError::InvalidEventCount:     return "InvalidEventCount";
        case TraceFileError::InvalidBlockLength:    return "InvalidBlockLength";
        case TraceFileError::ChecksumMismatch:      return "ChecksumMismatch";
        case TraceFileError::TruncatedFile:         return "TruncatedFile";
        case TraceFileError::MissingFooter:         return "MissingFooter";
        case TraceFileError::ArithmeticOverflow:    return "ArithmeticOverflow";
        case TraceFileError::BufferTooSmall:        return "BufferTooSmall";
        case TraceFileError::AlreadyOpen:           return "AlreadyOpen";
        case TraceFileError::NotOpen:               return "NotOpen";
        default:                                    return "UnknownError";
    }
}

// -----------------------------------------------------------------------------
// Metadata Structures
// -----------------------------------------------------------------------------
struct TraceFileHeader {
    std::uint64_t magic{TRACE_FILE_MAGIC};
    std::uint16_t file_format_version{TRACE_FILE_FORMAT_VERSION};
    std::uint16_t header_size{TRACE_FILE_HEADER_SIZE};
    std::uint32_t schema_version{TRACE_SCHEMA_VERSION};
    std::uint16_t record_size{TRACE_EVENT_DISK_SIZE};
    std::uint8_t  endianness{TRACE_ENDIANNESS_LITTLE};
    std::uint8_t  reserved0{0};
    std::uint32_t feature_flags{0};
    std::uint64_t creation_timestamp{0};
    std::uint8_t  reserved1[28]{0};
    std::uint32_t header_crc32c{0};
};

struct TraceBlockHeader {
    std::uint32_t block_magic{TRACE_BLOCK_MAGIC};
    std::uint16_t block_version{TRACE_BLOCK_VERSION};
    std::uint16_t header_size{TRACE_BLOCK_HEADER_SIZE};
    std::uint64_t block_sequence{0};
    std::uint32_t event_count{0};
    std::uint32_t payload_bytes{0};
    std::uint32_t payload_crc32c{0};
    std::uint32_t header_crc32c{0};
};

struct TraceFileFooter {
    std::uint64_t footer_magic{TRACE_FOOTER_MAGIC};
    std::uint16_t footer_version{TRACE_FOOTER_VERSION};
    std::uint16_t footer_size{TRACE_FILE_FOOTER_SIZE};
    std::uint64_t total_events{0};
    std::uint64_t total_blocks{0};
    std::uint32_t finalization_status{TRACE_STATUS_CLEANLY_FINALIZED};
    std::uint8_t  reserved[32]{0};
    std::uint32_t footer_crc32c{0};
};

// -----------------------------------------------------------------------------
// CRC32C Checksum API (Castagnoli polynomial 0x82F63B78)
// -----------------------------------------------------------------------------
std::uint32_t calculate_crc32c(const void* data, std::size_t length, std::uint32_t initial = 0xFFFFFFFFu) noexcept;

// -----------------------------------------------------------------------------
// Primitive Little-Endian Serialization Helpers
// -----------------------------------------------------------------------------
void encode_u16_le(std::uint8_t* dst, std::uint16_t val) noexcept;
void encode_u32_le(std::uint8_t* dst, std::uint32_t val) noexcept;
void encode_u64_le(std::uint8_t* dst, std::uint64_t val) noexcept;

[[nodiscard]] std::uint16_t decode_u16_le(const std::uint8_t* src) noexcept;
[[nodiscard]] std::uint32_t decode_u32_le(const std::uint8_t* src) noexcept;
[[nodiscard]] std::uint64_t decode_u64_le(const std::uint8_t* src) noexcept;

// -----------------------------------------------------------------------------
// Event Serialization & Deserialization
// -----------------------------------------------------------------------------
TraceFileError serialize_trace_event(const TraceEvent& event, std::uint8_t* dst, std::size_t dst_size) noexcept;
TraceFileError deserialize_trace_event(const std::uint8_t* src, std::size_t src_size, TraceEvent& out_event) noexcept;

// -----------------------------------------------------------------------------
// Metadata Serialization & Deserialization
// -----------------------------------------------------------------------------
TraceFileError serialize_file_header(const TraceFileHeader& hdr, std::uint8_t* dst, std::size_t dst_size) noexcept;
TraceFileError deserialize_file_header(const std::uint8_t* src, std::size_t src_size, TraceFileHeader& out_hdr) noexcept;

TraceFileError serialize_block_header(const TraceBlockHeader& blk, std::uint8_t* dst, std::size_t dst_size) noexcept;
TraceFileError deserialize_block_header(const std::uint8_t* src, std::size_t src_size, TraceBlockHeader& out_blk) noexcept;

TraceFileError serialize_file_footer(const TraceFileFooter& ftr, std::uint8_t* dst, std::size_t dst_size) noexcept;
TraceFileError deserialize_file_footer(const std::uint8_t* src, std::size_t src_size, TraceFileFooter& out_ftr) noexcept;

} // namespace winios::trace
