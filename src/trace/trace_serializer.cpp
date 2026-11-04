#include "trace_serializer.h"

#include <cstring>

namespace winios::trace {

namespace {

// CRC32C (Castagnoli) 256-entry lookup table for polynomial 0x82F63B78
constexpr std::uint32_t generate_crc32c_entry(std::uint32_t byte) noexcept {
    std::uint32_t crc = byte;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ ((crc & 1u) ? 0x82F63B78u : 0u);
    }
    return crc;
}

// Slicing-by-8 tables derived from the same polynomial. State representation
// and initial/final XOR conventions are byte-for-byte identical to the
// historical single-table loop; only the number of table lookups per byte
// changes. This keeps every persisted CRC and every state hash unchanged.
struct Crc32cTables {
    std::uint32_t table[8][256];
    constexpr Crc32cTables() : table{} {
        for (std::uint32_t i = 0; i < 256; ++i) {
            table[0][i] = generate_crc32c_entry(i);
        }
        for (std::size_t slice = 1; slice < 8; ++slice) {
            for (std::uint32_t i = 0; i < 256; ++i) {
                const std::uint32_t prev = table[slice - 1][i];
                table[slice][i] = table[0][prev & 0xFFu] ^ (prev >> 8);
            }
        }
    }
};

constexpr Crc32cTables kCrc32cTables{};

inline std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

} // anonymous namespace

std::uint32_t calculate_crc32c(const void* data, std::size_t length, std::uint32_t initial) noexcept {
    if (!data || length == 0) return 0;

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = initial;

    while (length >= 8) {
        const std::uint32_t low = read_u32_le(bytes) ^ crc;
        const std::uint32_t high = read_u32_le(bytes + 4);
        crc = kCrc32cTables.table[7][low & 0xFFu] ^
              kCrc32cTables.table[6][(low >> 8) & 0xFFu] ^
              kCrc32cTables.table[5][(low >> 16) & 0xFFu] ^
              kCrc32cTables.table[4][(low >> 24) & 0xFFu] ^
              kCrc32cTables.table[3][high & 0xFFu] ^
              kCrc32cTables.table[2][(high >> 8) & 0xFFu] ^
              kCrc32cTables.table[1][(high >> 16) & 0xFFu] ^
              kCrc32cTables.table[0][(high >> 24) & 0xFFu];
        bytes += 8;
        length -= 8;
    }

    while (length-- > 0) {
        crc = kCrc32cTables.table[0][(crc ^ *bytes++) & 0xFFu] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFu;
}

void encode_u16_le(std::uint8_t* dst, std::uint16_t val) noexcept {
    dst[0] = static_cast<std::uint8_t>(val & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((val >> 8) & 0xFFu);
}

void encode_u32_le(std::uint8_t* dst, std::uint32_t val) noexcept {
    dst[0] = static_cast<std::uint8_t>(val & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((val >> 8) & 0xFFu);
    dst[2] = static_cast<std::uint8_t>((val >> 16) & 0xFFu);
    dst[3] = static_cast<std::uint8_t>((val >> 24) & 0xFFu);
}

void encode_u64_le(std::uint8_t* dst, std::uint64_t val) noexcept {
    for (int i = 0; i < 8; ++i) {
        dst[i] = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFFu);
    }
}

std::uint16_t decode_u16_le(const std::uint8_t* src) noexcept {
    return static_cast<std::uint16_t>(src[0]) |
           (static_cast<std::uint16_t>(src[1]) << 8);
}

std::uint32_t decode_u32_le(const std::uint8_t* src) noexcept {
    return static_cast<std::uint32_t>(src[0]) |
           (static_cast<std::uint32_t>(src[1]) << 8) |
           (static_cast<std::uint32_t>(src[2]) << 16) |
           (static_cast<std::uint32_t>(src[3]) << 24);
}

std::uint64_t decode_u64_le(const std::uint8_t* src) noexcept {
    std::uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<std::uint64_t>(src[i]) << (i * 8));
    }
    return val;
}

TraceFileError serialize_trace_event(const TraceEvent& event, std::uint8_t* dst, std::size_t dst_size) noexcept {
    if (!dst || dst_size < TRACE_EVENT_DISK_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    // Validate event type
    const auto raw_type = event.event_type;
    const bool valid_type = (raw_type <= static_cast<std::uint16_t>(TraceEventType::TimerEvent)) ||
                            (raw_type >= static_cast<std::uint16_t>(TraceEventType::DroppedMarker) &&
                             raw_type <= static_cast<std::uint16_t>(TraceEventType::TraceEnd));
    if (!valid_type) {
        return TraceFileError::InvalidEventType;
    }

    if (event.exception_level > 3) {
        return TraceFileError::InvalidPrivilegeLevel;
    }

    if (event.address_space > 3) {
        return TraceFileError::InvalidAddressSpace;
    }

    encode_u64_le(dst + 0,  event.sequence_number);
    encode_u64_le(dst + 8,  event.virtual_time);
    encode_u64_le(dst + 16, event.pc);
    encode_u64_le(dst + 24, event.address);
    encode_u64_le(dst + 32, event.data);
    encode_u32_le(dst + 40, event.opcode_or_syndrome);
    encode_u32_le(dst + 44, event.flags);
    encode_u16_le(dst + 48, event.event_type);
    encode_u16_le(dst + 50, event.core_id);
    dst[52] = event.exception_level;
    dst[53] = event.address_space;
    encode_u16_le(dst + 54, event.access_size);
    encode_u64_le(dst + 56, event.reserved);

    return TraceFileError::Success;
}

TraceFileError deserialize_trace_event(const std::uint8_t* src, std::size_t src_size, TraceEvent& out_event) noexcept {
    if (!src || src_size < TRACE_EVENT_DISK_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    const std::uint16_t raw_type = decode_u16_le(src + 48);
    const bool valid_type = (raw_type <= static_cast<std::uint16_t>(TraceEventType::TimerEvent)) ||
                            (raw_type >= static_cast<std::uint16_t>(TraceEventType::DroppedMarker) &&
                             raw_type <= static_cast<std::uint16_t>(TraceEventType::TraceEnd));
    if (!valid_type) {
        return TraceFileError::InvalidEventType;
    }

    const std::uint8_t raw_el = src[52];
    if (raw_el > 3) {
        return TraceFileError::InvalidPrivilegeLevel;
    }

    const std::uint8_t raw_as = src[53];
    if (raw_as > 3) {
        return TraceFileError::InvalidAddressSpace;
    }

    const std::uint64_t reserved = decode_u64_le(src + 56);
    if (reserved != 0) {
        return TraceFileError::InvalidReservedField;
    }

    out_event.sequence_number    = decode_u64_le(src + 0);
    out_event.virtual_time       = decode_u64_le(src + 8);
    out_event.pc                 = decode_u64_le(src + 16);
    out_event.address            = decode_u64_le(src + 24);
    out_event.data               = decode_u64_le(src + 32);
    out_event.opcode_or_syndrome = decode_u32_le(src + 40);
    out_event.flags              = decode_u32_le(src + 44);
    out_event.event_type         = raw_type;
    out_event.core_id            = decode_u16_le(src + 50);
    out_event.exception_level    = raw_el;
    out_event.address_space      = raw_as;
    out_event.access_size        = decode_u16_le(src + 54);
    out_event.reserved           = 0;

    return TraceFileError::Success;
}

TraceFileError serialize_file_header(const TraceFileHeader& hdr, std::uint8_t* dst, std::size_t dst_size) noexcept {
    if (!dst || dst_size < TRACE_FILE_HEADER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    encode_u64_le(dst + 0,  hdr.magic);
    encode_u16_le(dst + 8,  hdr.file_format_version);
    encode_u16_le(dst + 10, hdr.header_size);
    encode_u32_le(dst + 12, hdr.schema_version);
    encode_u16_le(dst + 16, hdr.record_size);
    dst[18] = hdr.endianness;
    dst[19] = 0; // reserved0
    encode_u32_le(dst + 20, hdr.feature_flags);
    encode_u64_le(dst + 24, hdr.creation_timestamp);
    std::memset(dst + 32, 0, 28); // reserved1

    // Checksum over first 60 bytes
    const std::uint32_t crc = calculate_crc32c(dst, 60);
    encode_u32_le(dst + 60, crc);

    return TraceFileError::Success;
}

TraceFileError deserialize_file_header(const std::uint8_t* src, std::size_t src_size, TraceFileHeader& out_hdr) noexcept {
    if (!src || src_size < TRACE_FILE_HEADER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    const std::uint64_t magic = decode_u64_le(src + 0);
    if (magic != TRACE_FILE_MAGIC) {
        return TraceFileError::InvalidMagic;
    }

    const std::uint16_t version = decode_u16_le(src + 8);
    if (version != TRACE_FILE_FORMAT_VERSION) {
        return TraceFileError::UnsupportedVersion;
    }

    const std::uint16_t hdr_size = decode_u16_le(src + 10);
    if (hdr_size != TRACE_FILE_HEADER_SIZE) {
        return TraceFileError::InvalidHeaderSize;
    }

    const std::uint32_t schema = decode_u32_le(src + 12);
    if (schema != TRACE_SCHEMA_VERSION) {
        return TraceFileError::UnsupportedSchema;
    }

    const std::uint16_t record_size = decode_u16_le(src + 16);
    if (record_size != TRACE_EVENT_DISK_SIZE) {
        return TraceFileError::InvalidRecordSize;
    }

    const std::uint8_t endianness = src[18];
    if (endianness != TRACE_ENDIANNESS_LITTLE) {
        return TraceFileError::InvalidEndianness;
    }

    if (src[19] != 0) {
        return TraceFileError::InvalidReservedField;
    }

    // Verify reserved1 is zero
    for (int i = 32; i < 60; ++i) {
        if (src[i] != 0) {
            return TraceFileError::InvalidReservedField;
        }
    }

    // Checksum verification
    const std::uint32_t stored_crc = decode_u32_le(src + 60);
    const std::uint32_t computed_crc = calculate_crc32c(src, 60);
    if (stored_crc != computed_crc) {
        return TraceFileError::ChecksumMismatch;
    }

    out_hdr.magic               = magic;
    out_hdr.file_format_version = version;
    out_hdr.header_size         = hdr_size;
    out_hdr.schema_version      = schema;
    out_hdr.record_size         = record_size;
    out_hdr.endianness          = endianness;
    out_hdr.reserved0           = 0;
    out_hdr.feature_flags       = decode_u32_le(src + 20);
    out_hdr.creation_timestamp  = decode_u64_le(src + 24);
    std::memset(out_hdr.reserved1, 0, sizeof(out_hdr.reserved1));
    out_hdr.header_crc32c       = stored_crc;

    return TraceFileError::Success;
}

TraceFileError serialize_block_header(const TraceBlockHeader& blk, std::uint8_t* dst, std::size_t dst_size) noexcept {
    if (!dst || dst_size < TRACE_BLOCK_HEADER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    if (blk.event_count > MAX_EVENTS_PER_BLOCK) {
        return TraceFileError::InvalidEventCount;
    }

    // Check arithmetic overflow
    const std::uint64_t expected_bytes = static_cast<std::uint64_t>(blk.event_count) * TRACE_EVENT_DISK_SIZE;
    if (expected_bytes != blk.payload_bytes) {
        return TraceFileError::InvalidBlockLength;
    }

    encode_u32_le(dst + 0,  blk.block_magic);
    encode_u16_le(dst + 4,  blk.block_version);
    encode_u16_le(dst + 6,  blk.header_size);
    encode_u64_le(dst + 8,  blk.block_sequence);
    encode_u32_le(dst + 16, blk.event_count);
    encode_u32_le(dst + 20, blk.payload_bytes);
    encode_u32_le(dst + 24, blk.payload_crc32c);

    // Header checksum over bytes 0..27
    const std::uint32_t crc = calculate_crc32c(dst, 28);
    encode_u32_le(dst + 28, crc);

    return TraceFileError::Success;
}

TraceFileError deserialize_block_header(const std::uint8_t* src, std::size_t src_size, TraceBlockHeader& out_blk) noexcept {
    if (!src || src_size < TRACE_BLOCK_HEADER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    const std::uint32_t magic = decode_u32_le(src + 0);
    if (magic != TRACE_BLOCK_MAGIC) {
        return TraceFileError::InvalidMagic;
    }

    const std::uint16_t version = decode_u16_le(src + 4);
    if (version != TRACE_BLOCK_VERSION) {
        return TraceFileError::UnsupportedVersion;
    }

    const std::uint16_t hdr_size = decode_u16_le(src + 6);
    if (hdr_size != TRACE_BLOCK_HEADER_SIZE) {
        return TraceFileError::InvalidHeaderSize;
    }

    const std::uint32_t event_count = decode_u32_le(src + 16);
    if (event_count > MAX_EVENTS_PER_BLOCK) {
        return TraceFileError::InvalidEventCount;
    }

    const std::uint32_t payload_bytes = decode_u32_le(src + 20);
    if (static_cast<std::uint64_t>(event_count) * TRACE_EVENT_DISK_SIZE != payload_bytes) {
        return TraceFileError::InvalidBlockLength;
    }

    const std::uint32_t stored_crc = decode_u32_le(src + 28);
    const std::uint32_t computed_crc = calculate_crc32c(src, 28);
    if (stored_crc != computed_crc) {
        return TraceFileError::ChecksumMismatch;
    }

    out_blk.block_magic    = magic;
    out_blk.block_version  = version;
    out_blk.header_size    = hdr_size;
    out_blk.block_sequence = decode_u64_le(src + 8);
    out_blk.event_count    = event_count;
    out_blk.payload_bytes  = payload_bytes;
    out_blk.payload_crc32c = decode_u32_le(src + 24);
    out_blk.header_crc32c  = stored_crc;

    return TraceFileError::Success;
}

TraceFileError serialize_file_footer(const TraceFileFooter& ftr, std::uint8_t* dst, std::size_t dst_size) noexcept {
    if (!dst || dst_size < TRACE_FILE_FOOTER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    encode_u64_le(dst + 0,  ftr.footer_magic);
    encode_u16_le(dst + 8,  ftr.footer_version);
    encode_u16_le(dst + 10, ftr.footer_size);
    encode_u64_le(dst + 12, ftr.total_events);
    encode_u64_le(dst + 20, ftr.total_blocks);
    encode_u32_le(dst + 28, ftr.finalization_status);
    std::memset(dst + 32, 0, 28); // reserved

    // Checksum over bytes 0..59
    const std::uint32_t crc = calculate_crc32c(dst, 60);
    encode_u32_le(dst + 60, crc);

    return TraceFileError::Success;
}

TraceFileError deserialize_file_footer(const std::uint8_t* src, std::size_t src_size, TraceFileFooter& out_ftr) noexcept {
    if (!src || src_size < TRACE_FILE_FOOTER_SIZE) {
        return TraceFileError::BufferTooSmall;
    }

    const std::uint64_t magic = decode_u64_le(src + 0);
    if (magic != TRACE_FOOTER_MAGIC) {
        return TraceFileError::InvalidMagic;
    }

    const std::uint16_t version = decode_u16_le(src + 8);
    if (version != TRACE_FOOTER_VERSION) {
        return TraceFileError::UnsupportedVersion;
    }

    const std::uint16_t ftr_size = decode_u16_le(src + 10);
    if (ftr_size != TRACE_FILE_FOOTER_SIZE) {
        return TraceFileError::InvalidHeaderSize;
    }

    const std::uint32_t status = decode_u32_le(src + 28);
    if (status != TRACE_STATUS_CLEANLY_FINALIZED) {
        return TraceFileError::MissingFooter;
    }

    for (int i = 32; i < 60; ++i) {
        if (src[i] != 0) {
            return TraceFileError::InvalidReservedField;
        }
    }

    const std::uint32_t stored_crc = decode_u32_le(src + 60);
    const std::uint32_t computed_crc = calculate_crc32c(src, 60);
    if (stored_crc != computed_crc) {
        return TraceFileError::ChecksumMismatch;
    }

    out_ftr.footer_magic         = magic;
    out_ftr.footer_version       = version;
    out_ftr.footer_size          = ftr_size;
    out_ftr.total_events         = decode_u64_le(src + 12);
    out_ftr.total_blocks         = decode_u64_le(src + 20);
    out_ftr.finalization_status  = status;
    std::memset(out_ftr.reserved, 0, sizeof(out_ftr.reserved));
    out_ftr.footer_crc32c        = stored_crc;

    return TraceFileError::Success;
}

} // namespace winios::trace
