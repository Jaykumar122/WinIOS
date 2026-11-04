#include "sndl/sndl_format.h"
#include "trace/trace_serializer.h"
#include <cstring>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// WSndlHeader Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_header(const WSndlHeader& hdr, std::uint8_t* dst) noexcept {
    std::memcpy(dst, hdr.magic.data(), 8);
    trace::encode_u16_le(dst + 8,  hdr.version);
    trace::encode_u16_le(dst + 10, hdr.core_count);
    trace::encode_u32_le(dst + 12, hdr.flags);
    trace::encode_u64_le(dst + 16, hdr.base_snapshot_hash);
    trace::encode_u64_le(dst + 24, hdr.creation_timestamp_ns);
    trace::encode_u64_le(dst + 32, hdr.total_record_count);
    trace::encode_u64_le(dst + 40, hdr.total_instructions);
    std::memcpy(dst + 48, hdr.snapshot_id.data(), 12);

    const std::uint32_t crc = trace::calculate_crc32c(dst, 60);
    trace::encode_u32_le(dst + 60, crc);
}

SndlResult decode_sndl_header(const std::uint8_t* src, std::size_t size, WSndlHeader& out_hdr) noexcept {
    if (size < kWSndlHeaderSize) {
        return SndlResult::fail(SndlErrorCode::FileTooShort, "Buffer too small for SNDL header");
    }

    if (std::memcmp(src, kWSndlMagic.data(), 8) != 0) {
        return SndlResult::fail(SndlErrorCode::InvalidMagic, "Invalid SNDL header magic identifier");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t stored_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != stored_crc) {
        return SndlResult::fail(SndlErrorCode::HeaderCrcMismatch, "SNDL header CRC32C checksum mismatch");
    }

    std::memcpy(out_hdr.magic.data(), src, 8);
    out_hdr.version = trace::decode_u16_le(src + 8);
    if (out_hdr.version != kWSndlCurrentVersion) {
        return SndlResult::fail(SndlErrorCode::UnsupportedVersion, "Unsupported SNDL format version: " + std::to_string(out_hdr.version));
    }

    out_hdr.core_count            = trace::decode_u16_le(src + 10);
    out_hdr.flags                 = trace::decode_u32_le(src + 12);
    out_hdr.base_snapshot_hash    = trace::decode_u64_le(src + 16);
    out_hdr.creation_timestamp_ns = trace::decode_u64_le(src + 24);
    out_hdr.total_record_count    = trace::decode_u64_le(src + 32);
    out_hdr.total_instructions    = trace::decode_u64_le(src + 40);
    std::memcpy(out_hdr.snapshot_id.data(), src + 48, 12);
    out_hdr.header_crc32c         = stored_crc;

    return SndlResult::ok();
}

// -----------------------------------------------------------------------------
// WSndlBlockHeader Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_block_header(const WSndlBlockHeader& blk, std::uint8_t* dst) noexcept {
    std::memcpy(dst, blk.magic.data(), 4);
    trace::encode_u16_le(dst + 4,  blk.version);
    trace::encode_u16_le(dst + 6,  blk.flags);
    trace::encode_u32_le(dst + 8,  blk.record_count);
    trace::encode_u32_le(dst + 12, blk.payload_bytes);
    trace::encode_u64_le(dst + 16, blk.first_sequence);
    trace::encode_u64_le(dst + 24, blk.last_sequence);
    trace::encode_u32_le(dst + 32, blk.payload_crc32c);
    std::memset(dst + 36, 0, 8);

    const std::uint32_t crc = trace::calculate_crc32c(dst, 44);
    trace::encode_u32_le(dst + 44, crc);
}

SndlResult decode_sndl_block_header(const std::uint8_t* src, std::size_t size, WSndlBlockHeader& out_blk) noexcept {
    if (size < kWSndlBlockHeaderSize) {
        return SndlResult::fail(SndlErrorCode::TruncatedBlock, "Buffer too small for block header");
    }

    if (std::memcmp(src, kWSndlBlockMagic.data(), 4) != 0) {
        return SndlResult::fail(SndlErrorCode::InvalidMagic, "Invalid block header magic");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 44);
    const std::uint32_t stored_crc = trace::decode_u32_le(src + 44);
    if (expected_crc != stored_crc) {
        return SndlResult::fail(SndlErrorCode::BlockHeaderCrcMismatch, "Block header CRC32C checksum mismatch");
    }

    std::memcpy(out_blk.magic.data(), src, 4);
    out_blk.version        = trace::decode_u16_le(src + 4);
    out_blk.flags          = trace::decode_u16_le(src + 6);
    out_blk.record_count   = trace::decode_u32_le(src + 8);
    out_blk.payload_bytes  = trace::decode_u32_le(src + 12);
    out_blk.first_sequence = trace::decode_u64_le(src + 16);
    out_blk.last_sequence  = trace::decode_u64_le(src + 24);
    out_blk.payload_crc32c = trace::decode_u32_le(src + 32);
    out_blk.header_crc32c  = stored_crc;

    return SndlResult::ok();
}

// -----------------------------------------------------------------------------
// WSndlFooter Encoding & Decoding
// -----------------------------------------------------------------------------
void encode_sndl_footer(const WSndlFooter& ftr, std::uint8_t* dst) noexcept {
    std::memcpy(dst, ftr.magic.data(), 8);
    trace::encode_u16_le(dst + 8,  ftr.version);
    trace::encode_u16_le(dst + 10, static_cast<std::uint16_t>(ftr.finalization_status));
    trace::encode_u32_le(dst + 12, ftr.block_count);
    trace::encode_u64_le(dst + 16, ftr.total_records);
    trace::encode_u64_le(dst + 24, ftr.last_sequence);
    trace::encode_u64_le(dst + 32, ftr.total_instructions);
    std::memset(dst + 40, 0, 20);

    const std::uint32_t crc = trace::calculate_crc32c(dst, 60);
    trace::encode_u32_le(dst + 60, crc);
}

SndlResult decode_sndl_footer(const std::uint8_t* src, std::size_t size, WSndlFooter& out_ftr) noexcept {
    if (size < kWSndlFooterSize) {
        return SndlResult::fail(SndlErrorCode::FileTooShort, "Buffer too small for footer");
    }

    if (std::memcmp(src, kWSndlFooterMagic.data(), 8) != 0) {
        return SndlResult::fail(SndlErrorCode::InvalidMagic, "Invalid footer magic");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t stored_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != stored_crc) {
        return SndlResult::fail(SndlErrorCode::FooterCrcMismatch, "Footer CRC32C checksum mismatch");
    }

    std::memcpy(out_ftr.magic.data(), src, 8);
    out_ftr.version             = trace::decode_u16_le(src + 8);
    out_ftr.finalization_status = static_cast<SndlFinalizationStatus>(trace::decode_u16_le(src + 10));
    out_ftr.block_count         = trace::decode_u32_le(src + 12);
    out_ftr.total_records       = trace::decode_u64_le(src + 16);
    out_ftr.last_sequence       = trace::decode_u64_le(src + 24);
    out_ftr.total_instructions   = trace::decode_u64_le(src + 32);
    out_ftr.footer_crc32c       = stored_crc;

    return SndlResult::ok();
}

} // namespace winios::sndl
