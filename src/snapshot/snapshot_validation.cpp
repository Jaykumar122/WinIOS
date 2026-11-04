#include "snapshot/snapshot_validation.h"
#include "trace/trace_serializer.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace winios::snapshot {

namespace {

struct ByteRange {
    std::uint64_t start{0};
    std::uint64_t end{0}; // exclusive
    std::string name;

    bool overlaps(const ByteRange& other) const noexcept {
        return start < other.end && other.start < end;
    }
};

} // namespace

SnapshotResult parse_and_validate_directory(
    const std::uint8_t* data,
    std::size_t size,
    const WSnapHeader& header,
    std::vector<WSnapSectionDescriptor>& out_descriptors) noexcept {

    out_descriptors.clear();
    const std::uint64_t dir_offset = header.section_dir_offset;
    const std::uint64_t section_count = header.section_count;

    if (section_count > 1024) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidSectionDirectory, "Section count exceeds maximum limit (1024)");
    }

    const std::uint64_t dir_bytes = section_count * kWSnapSectionDescriptorSize;
    if (dir_offset > size || dir_bytes > size - dir_offset) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidSectionDirectory, "Section directory extends beyond snapshot file size");
    }

    std::vector<ByteRange> occupied_ranges;
    occupied_ranges.push_back(ByteRange{0, kWSnapHeaderSize, "Header"});
    occupied_ranges.push_back(ByteRange{dir_offset, dir_offset + dir_bytes, "SectionDirectory"});

    out_descriptors.reserve(static_cast<std::size_t>(section_count));
    const std::uint8_t* dir_ptr = data + dir_offset;

    for (std::size_t i = 0; i < section_count; ++i) {
        WSnapSectionDescriptor desc{};
        const auto res = decode_section_descriptor(dir_ptr + i * kWSnapSectionDescriptorSize, kWSnapSectionDescriptorSize, desc);
        if (!res.success) {
            return res;
        }

        // Check for integer overflow in section payload boundaries
        if (desc.file_offset > size || desc.compressed_size > size - desc.file_offset) {
            return SnapshotResult::fail(SnapshotErrorCode::OversizedSection,
                "Section " + std::string(section_type_name(desc.section_type)) + " extends beyond file boundary");
        }

        // Validate payload CRC32C
        if (desc.compressed_size > 0) {
            const std::uint32_t computed_crc = trace::calculate_crc32c(data + desc.file_offset, static_cast<std::size_t>(desc.compressed_size));
            if (computed_crc != desc.section_crc32c) {
                return SnapshotResult::fail(SnapshotErrorCode::SectionCrcMismatch,
                    "CRC32C mismatch in section " + std::string(section_type_name(desc.section_type)));
            }

            ByteRange sec_range{
                desc.file_offset,
                desc.file_offset + desc.compressed_size,
                std::string(section_type_name(desc.section_type)) + "#" + std::to_string(desc.section_id)
            };

            for (const auto& r : occupied_ranges) {
                if (sec_range.overlaps(r)) {
                    return SnapshotResult::fail(SnapshotErrorCode::OverlappingSections,
                        "Section " + sec_range.name + " overlaps with " + r.name);
                }
            }
            occupied_ranges.push_back(sec_range);
        }

        out_descriptors.push_back(desc);
    }

    return SnapshotResult::ok();
}

SnapshotValidationResult validate_snapshot_buffer(
    const std::uint8_t* data,
    std::size_t size) noexcept {

    std::vector<std::string> diagnostics;

    if (!data || size < kWSnapHeaderSize) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::TruncatedFile, "File size smaller than 64-byte WSnap header");
    }

    WSnapHeader header{};
    const auto header_res = decode_wsnap_header(data, size, header);
    if (!header_res.success) {
        return SnapshotValidationResult::fail(header_res.error_code, header_res.error_message);
    }

    diagnostics.push_back("Header OK: version=" + std::to_string(header.version) +
                          ", cores=" + std::to_string(header.core_count) +
                          ", mem_capacity=" + std::to_string(header.memory_capacity) +
                          ", sections=" + std::to_string(header.section_count));

    if (header.core_count == 0 || header.core_count > 128) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidStateData, "Invalid core count in header");
    }

    if (header.total_file_size != 0 && header.total_file_size != size) {
        diagnostics.push_back("Warning: header total_file_size (" + std::to_string(header.total_file_size) +
                              ") differs from actual size (" + std::to_string(size) + ")");
    }

    std::vector<WSnapSectionDescriptor> descriptors;
    const auto dir_res = parse_and_validate_directory(data, size, header, descriptors);
    if (!dir_res.success) {
        return SnapshotValidationResult::fail(dir_res.error_code, dir_res.error_message, diagnostics);
    }

    bool has_cpu = false;
    bool has_memory = false;
    for (const auto& d : descriptors) {
        if (d.section_type == SnapshotSectionType::CpuState && d.section_id == 0) {
            has_cpu = true;
        }
        if (d.section_type == SnapshotSectionType::MemoryState) {
            has_memory = true;
        }
        diagnostics.push_back("Section validated: " + std::string(section_type_name(d.section_type)) +
                              " id=" + std::to_string(d.section_id) +
                              " offset=" + std::to_string(d.file_offset) +
                              " size=" + std::to_string(d.compressed_size) +
                              " crc=0x" + std::to_string(d.section_crc32c));
    }

    if (!has_cpu) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Snapshot is missing required primary CPU_STATE section", diagnostics);
    }
    if (!has_memory) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Snapshot is missing required MEMORY_STATE section", diagnostics);
    }

    return SnapshotValidationResult::ok();
}

SnapshotValidationResult validate_snapshot_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::FileNotFound, "Could not open file: " + path.string());
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(kWSnapHeaderSize)) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::TruncatedFile, "File size smaller than header");
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::ReadError, "Failed to read file contents");
    }

    return validate_snapshot_buffer(buffer.data(), buffer.size());
}

} // namespace winios::snapshot
