#include "sndl/sndl_v2_format.h"
#include "trace/trace_serializer.h"

#include <cstring>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// Header Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_header(const WSndlV2Header& hdr, std::uint8_t* dst) noexcept {
    std::memcpy(dst, hdr.magic.data(), 8);
    trace::encode_u16_le(dst + 8, hdr.version);
    trace::encode_u16_le(dst + 10, hdr.core_count);
    trace::encode_u16_le(dst + 12, hdr.flags);
    trace::encode_u16_le(dst + 14, hdr.compression_type);
    trace::encode_u64_le(dst + 16, hdr.base_snapshot_hash);
    trace::encode_u64_le(dst + 24, hdr.base_snapshot_file_size);
    trace::encode_u64_le(dst + 32, hdr.memory_capacity);
    trace::encode_u64_le(dst + 40, hdr.initial_global_sequence);
    trace::encode_u32_le(dst + 48, hdr.total_events);
    trace::encode_u32_le(dst + 52, hdr.total_blocks);
    trace::encode_u32_le(dst + 56, 0); // reserved

    const std::uint32_t crc = trace::calculate_crc32c(dst, 60);
    trace::encode_u32_le(dst + 60, crc);
}

SndlV2Result decode_sndl_v2_header(const std::uint8_t* src, std::size_t size, WSndlV2Header& out_hdr) noexcept {
    if (size < kWSndlV2HeaderSize) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "Buffer too small for SNDL v2 header");
    }

    std::array<std::uint8_t, 8> magic{};
    std::memcpy(magic.data(), src, 8);
    if (magic != kWSndlV2Magic) {
        return SndlV2Result::fail(SndlV2ErrorCode::InvalidMagic, "Invalid SNDL v2 magic identifier");
    }

    const std::uint16_t version = trace::decode_u16_le(src + 8);
    if (version != kWSndlVersion2) {
        return SndlV2Result::fail(SndlV2ErrorCode::UnsupportedVersion, "Unsupported SNDL version (expected v2)");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t actual_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != actual_crc) {
        return SndlV2Result::fail(SndlV2ErrorCode::HeaderCrcMismatch, "SNDL v2 header CRC32C mismatch");
    }

    out_hdr.magic = magic;
    out_hdr.version = version;
    out_hdr.core_count = trace::decode_u16_le(src + 10);
    out_hdr.flags = trace::decode_u16_le(src + 12);
    out_hdr.compression_type = trace::decode_u16_le(src + 14);
    out_hdr.base_snapshot_hash = trace::decode_u64_le(src + 16);
    out_hdr.base_snapshot_file_size = trace::decode_u64_le(src + 24);
    out_hdr.memory_capacity = trace::decode_u64_le(src + 32);
    out_hdr.initial_global_sequence = trace::decode_u64_le(src + 40);
    out_hdr.total_events = trace::decode_u32_le(src + 48);
    out_hdr.total_blocks = trace::decode_u32_le(src + 52);
    out_hdr.header_crc32c = actual_crc;

    if (out_hdr.core_count == 0) {
        return SndlV2Result::fail(SndlV2ErrorCode::InvalidCoreId, "Header specifies 0 cores");
    }

    return SndlV2Result::ok();
}

// -----------------------------------------------------------------------------
// Block Header Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_block_header(const WSndlV2BlockHeader& blk, std::uint8_t* dst) noexcept {
    std::memcpy(dst, blk.magic.data(), 4);
    trace::encode_u16_le(dst + 4, blk.version);
    trace::encode_u16_le(dst + 6, blk.flags);
    trace::encode_u64_le(dst + 8, blk.first_global_sequence);
    trace::encode_u64_le(dst + 16, blk.last_global_sequence);
    trace::encode_u32_le(dst + 24, blk.block_index);
    trace::encode_u32_le(dst + 28, blk.record_count);
    trace::encode_u32_le(dst + 32, blk.payload_bytes);
    trace::encode_u32_le(dst + 36, blk.payload_crc32c);
    trace::encode_u32_le(dst + 40, 0); // reserved

    const std::uint32_t crc = trace::calculate_crc32c(dst, 44);
    trace::encode_u32_le(dst + 44, crc);
}

SndlV2Result decode_sndl_v2_block_header(const std::uint8_t* src, std::size_t size, WSndlV2BlockHeader& out_blk) noexcept {
    if (size < kWSndlV2BlockHeaderSize) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "Buffer too small for SNDL v2 block header");
    }

    std::array<std::uint8_t, 4> magic{};
    std::memcpy(magic.data(), src, 4);
    if (magic != kWSndlV2BlockMagic) {
        return SndlV2Result::fail(SndlV2ErrorCode::InvalidMagic, "Invalid SNDL v2 block magic identifier");
    }

    const std::uint16_t version = trace::decode_u16_le(src + 4);
    if (version != kWSndlVersion2) {
        return SndlV2Result::fail(SndlV2ErrorCode::UnsupportedVersion, "Unsupported SNDL block version");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 44);
    const std::uint32_t actual_crc = trace::decode_u32_le(src + 44);
    if (expected_crc != actual_crc) {
        return SndlV2Result::fail(SndlV2ErrorCode::BlockHeaderCrcMismatch, "SNDL v2 block header CRC32C mismatch");
    }

    out_blk.magic = magic;
    out_blk.version = version;
    out_blk.flags = trace::decode_u16_le(src + 6);
    out_blk.first_global_sequence = trace::decode_u64_le(src + 8);
    out_blk.last_global_sequence = trace::decode_u64_le(src + 16);
    out_blk.block_index = trace::decode_u32_le(src + 24);
    out_blk.record_count = trace::decode_u32_le(src + 28);
    out_blk.payload_bytes = trace::decode_u32_le(src + 32);
    out_blk.payload_crc32c = trace::decode_u32_le(src + 36);
    out_blk.reserved = trace::decode_u32_le(src + 40);
    out_blk.header_crc32c = actual_crc;

    return SndlV2Result::ok();
}

// -----------------------------------------------------------------------------
// Record Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_record(const SndlV2Record& rec, std::uint8_t* dst) noexcept {
    trace::encode_u64_le(dst + 0, rec.global_event_sequence);
    trace::encode_u64_le(dst + 8, rec.global_retirement_sequence);
    trace::encode_u64_le(dst + 16, rec.core_retirement_count);
    trace::encode_u64_le(dst + 24, rec.pc);
    trace::encode_u64_le(dst + 32, rec.payload1);
    trace::encode_u64_le(dst + 40, rec.payload2);
    trace::encode_u64_le(dst + 48, rec.payload3);
    trace::encode_u32_le(dst + 56, rec.core_id);
    trace::encode_u32_le(dst + 60, rec.source_id);
    trace::encode_u32_le(dst + 64, rec.flags);
    trace::encode_u16_le(dst + 68, rec.sub_instruction_ordinal);
    trace::encode_u16_le(dst + 70, static_cast<std::uint16_t>(rec.event_type));
    dst[72] = static_cast<std::uint8_t>(rec.boundary_type);
    dst[73] = 0; // reserved
    trace::encode_u16_le(dst + 74, rec.payload_size);

    const std::uint32_t crc = trace::calculate_crc32c(dst, 76);
    trace::encode_u32_le(dst + 76, crc);
}

SndlV2Result decode_sndl_v2_record(const std::uint8_t* src, std::size_t size, SndlV2Record& out_rec) noexcept {
    if (size < kWSndlV2RecordSize) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "Buffer too small for SNDL v2 record");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 76);
    const std::uint32_t actual_crc = trace::decode_u32_le(src + 76);
    if (expected_crc != actual_crc) {
        return SndlV2Result::fail(SndlV2ErrorCode::RecordCrcMismatch, "SNDL v2 record CRC32C mismatch");
    }

    out_rec.global_event_sequence = trace::decode_u64_le(src + 0);
    out_rec.global_retirement_sequence = trace::decode_u64_le(src + 8);
    out_rec.core_retirement_count = trace::decode_u64_le(src + 16);
    out_rec.pc = trace::decode_u64_le(src + 24);
    out_rec.payload1 = trace::decode_u64_le(src + 32);
    out_rec.payload2 = trace::decode_u64_le(src + 40);
    out_rec.payload3 = trace::decode_u64_le(src + 48);
    out_rec.core_id = trace::decode_u32_le(src + 56);
    out_rec.source_id = trace::decode_u32_le(src + 60);
    out_rec.flags = trace::decode_u32_le(src + 64);
    out_rec.sub_instruction_ordinal = trace::decode_u16_le(src + 68);
    out_rec.event_type = static_cast<SndlV2EventType>(trace::decode_u16_le(src + 70));
    out_rec.boundary_type = static_cast<SndlV2BoundaryType>(src[72]);
    out_rec.reserved = src[73];
    out_rec.payload_size = trace::decode_u16_le(src + 74);
    out_rec.record_crc32c = actual_crc;

    return SndlV2Result::ok();
}

// -----------------------------------------------------------------------------
// Footer Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_footer(const WSndlV2Footer& ftr, std::uint8_t* dst) noexcept {
    std::memcpy(dst, ftr.magic.data(), 8);
    trace::encode_u64_le(dst + 8, ftr.last_global_sequence);
    trace::encode_u64_le(dst + 16, ftr.last_global_retirement_sequence);
    trace::encode_u64_le(dst + 24, ftr.total_events);
    trace::encode_u64_le(dst + 32, ftr.dropped_events);
    trace::encode_u32_le(dst + 40, ftr.total_blocks);
    trace::encode_u16_le(dst + 44, ftr.version);
    trace::encode_u16_le(dst + 46, static_cast<std::uint16_t>(ftr.completion_status));
    std::memset(dst + 48, 0, 12); // reserved

    const std::uint32_t crc = trace::calculate_crc32c(dst, 60);
    trace::encode_u32_le(dst + 60, crc);
}

SndlV2Result decode_sndl_v2_footer(const std::uint8_t* src, std::size_t size, WSndlV2Footer& out_ftr) noexcept {
    if (size < kWSndlV2FooterSize) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "Buffer too small for SNDL v2 footer");
    }

    std::array<std::uint8_t, 8> magic{};
    std::memcpy(magic.data(), src, 8);
    if (magic != kWSndlV2FooterMagic) {
        return SndlV2Result::fail(SndlV2ErrorCode::InvalidMagic, "Invalid SNDL v2 footer magic identifier");
    }

    const std::uint16_t version = trace::decode_u16_le(src + 44);
    if (version != kWSndlVersion2) {
        return SndlV2Result::fail(SndlV2ErrorCode::UnsupportedVersion, "Unsupported SNDL footer version");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t actual_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != actual_crc) {
        return SndlV2Result::fail(SndlV2ErrorCode::FooterCrcMismatch, "SNDL v2 footer CRC32C mismatch");
    }

    out_ftr.magic = magic;
    out_ftr.last_global_sequence = trace::decode_u64_le(src + 8);
    out_ftr.last_global_retirement_sequence = trace::decode_u64_le(src + 16);
    out_ftr.total_events = trace::decode_u64_le(src + 24);
    out_ftr.dropped_events = trace::decode_u64_le(src + 32);
    out_ftr.total_blocks = trace::decode_u32_le(src + 40);
    out_ftr.version = version;
    out_ftr.completion_status = static_cast<SndlV2CompletionStatus>(trace::decode_u16_le(src + 46));
    out_ftr.footer_crc32c = actual_crc;

    return SndlV2Result::ok();
}

} // namespace winios::sndl
