#include "snapshot/wsnap_format.h"
#include "trace/trace_serializer.h"
#include <cstring>

namespace winios::snapshot {

void encode_wsnap_header(const WSnapHeader& header, std::uint8_t* dst) noexcept {
    std::memcpy(dst, header.magic.data(), 8);
    trace::encode_u16_le(dst + 8, header.version);
    trace::encode_u16_le(dst + 10, header.core_count);
    trace::encode_u16_le(dst + 12, static_cast<std::uint16_t>(header.compression_type));
    trace::encode_u16_le(dst + 14, header.flags);
    trace::encode_u64_le(dst + 16, header.timestamp_ns);
    trace::encode_u64_le(dst + 24, header.memory_capacity);
    trace::encode_u64_le(dst + 32, header.section_dir_offset);
    trace::encode_u32_le(dst + 40, header.section_count);
    trace::encode_u32_le(dst + 44, header.total_file_size);
    trace::encode_u32_le(dst + 48, header.reserved0);
    trace::encode_u32_le(dst + 52, header.reserved1);
    trace::encode_u32_le(dst + 56, header.reserved2);

    const std::uint32_t crc = trace::calculate_crc32c(dst, 60);
    trace::encode_u32_le(dst + 60, crc);
}

SnapshotResult decode_wsnap_header(const std::uint8_t* src, std::size_t size, WSnapHeader& out_header) noexcept {
    if (!src || size < kWSnapHeaderSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for WSnap header");
    }

    if (std::memcmp(src, kWSnapMagic.data(), 8) != 0) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidMagic, "Header magic does not match WSNAP specification");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t stored_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != stored_crc) {
        return SnapshotResult::fail(SnapshotErrorCode::HeaderCrcMismatch, "Header CRC32C verification failed");
    }

    std::memcpy(out_header.magic.data(), src, 8);
    out_header.version = trace::decode_u16_le(src + 8);
    if (out_header.version != kWSnapCurrentVersion) {
        return SnapshotResult::fail(SnapshotErrorCode::UnsupportedVersion, "Unsupported WSnap format version");
    }

    out_header.core_count = trace::decode_u16_le(src + 10);
    out_header.compression_type = static_cast<SnapshotCompressionType>(trace::decode_u16_le(src + 12));
    out_header.flags = trace::decode_u16_le(src + 14);
    out_header.timestamp_ns = trace::decode_u64_le(src + 16);
    out_header.memory_capacity = trace::decode_u64_le(src + 24);
    out_header.section_dir_offset = trace::decode_u64_le(src + 32);
    out_header.section_count = trace::decode_u32_le(src + 40);
    out_header.total_file_size = trace::decode_u32_le(src + 44);
    out_header.reserved0 = trace::decode_u32_le(src + 48);
    out_header.reserved1 = trace::decode_u32_le(src + 52);
    out_header.reserved2 = trace::decode_u32_le(src + 56);
    out_header.header_crc32c = stored_crc;

    return SnapshotResult::ok();
}

void encode_section_descriptor(const WSnapSectionDescriptor& desc, std::uint8_t* dst) noexcept {
    trace::encode_u32_le(dst + 0, static_cast<std::uint32_t>(desc.section_type));
    trace::encode_u32_le(dst + 4, desc.section_id);
    trace::encode_u64_le(dst + 8, desc.file_offset);
    trace::encode_u64_le(dst + 16, desc.compressed_size);
    trace::encode_u64_le(dst + 24, desc.uncompressed_size);
    trace::encode_u32_le(dst + 32, desc.section_crc32c);
    trace::encode_u16_le(dst + 36, desc.flags);
    trace::encode_u16_le(dst + 38, desc.reserved);
}

SnapshotResult decode_section_descriptor(const std::uint8_t* src, std::size_t size, WSnapSectionDescriptor& out_desc) noexcept {
    if (!src || size < kWSnapSectionDescriptorSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for section descriptor");
    }

    out_desc.section_type = static_cast<SnapshotSectionType>(trace::decode_u32_le(src + 0));
    out_desc.section_id = trace::decode_u32_le(src + 4);
    out_desc.file_offset = trace::decode_u64_le(src + 8);
    out_desc.compressed_size = trace::decode_u64_le(src + 16);
    out_desc.uncompressed_size = trace::decode_u64_le(src + 24);
    out_desc.section_crc32c = trace::decode_u32_le(src + 32);
    out_desc.flags = trace::decode_u16_le(src + 36);
    out_desc.reserved = trace::decode_u16_le(src + 38);

    return SnapshotResult::ok();
}

} // namespace winios::snapshot
