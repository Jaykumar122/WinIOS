#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_validation.h"
#include "trace/trace_serializer.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::snapshot {

SnapshotResult SnapshotManager::capture_to_buffer(
    const SnapshotRestoreContext& context,
    std::vector<std::uint8_t>& out_buffer,
    const SnapshotCaptureOptions& options) {

    if (!context.primary_cpu) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidArgument, "Primary CPU pointer cannot be null");
    }

    const std::uint16_t total_cores = static_cast<std::uint16_t>(1 + context.secondary_cpus.size());

    struct PendingSection {
        SnapshotSectionType type;
        std::uint32_t id;
        std::vector<std::uint8_t> payload;
    };
    std::vector<PendingSection> pending;

    // 1. Primary CPU Core 0
    pending.push_back({SnapshotSectionType::CpuState, 0, serialize_cpu_state_with_retirement(*context.primary_cpu)});
    if (options.include_pmu) {
        pending.push_back({SnapshotSectionType::PmuState, 0, serialize_pmu_state(context.primary_cpu->get_pmu(), 0)});
    }
    if (options.include_debug) {
        pending.push_back({SnapshotSectionType::DebugState, 0, serialize_debug_state(context.primary_cpu->get_debug_arch(), 0)});
    }

    // 2. Secondary CPU Cores
    for (std::size_t i = 0; i < context.secondary_cpus.size(); ++i) {
        CPU* sec = context.secondary_cpus[i];
        if (sec) {
            const auto cid = static_cast<std::uint32_t>(i + 1);
            pending.push_back({SnapshotSectionType::CpuState, cid, serialize_cpu_state_with_retirement(*sec)});
            if (options.include_pmu) {
                pending.push_back({SnapshotSectionType::PmuState, cid, serialize_pmu_state(sec->get_pmu(), cid)});
            }
            if (options.include_debug) {
                pending.push_back({SnapshotSectionType::DebugState, cid, serialize_debug_state(sec->get_debug_arch(), cid)});
            }
        }
    }

    // 3. Guest Physical Memory
    if (context.memory && options.include_memory) {
        pending.push_back({SnapshotSectionType::MemoryState, 0, serialize_memory_state(*context.memory)});
    }

    // 4. Timers & VirtualTime
    if (context.virtual_time && options.include_timers) {
        pending.push_back({SnapshotSectionType::TimerState, 0, serialize_timer_state(*context.virtual_time, total_cores)});
    }

    // 5. GIC State
    if (context.gic && options.include_gic) {
        pending.push_back({SnapshotSectionType::GicState, 0, serialize_gic_state(*context.gic)});
    }

    // 6. Metadata
    pending.push_back({SnapshotSectionType::Metadata, 0, serialize_metadata(options.description, options.creator)});

    // Layout the file:
    // [Header (64 bytes)]
    // [Section 0 Payload...]
    // [Section 1 Payload...]
    // ...
    // [Section Directory Table (section_count * 40 bytes)]

    std::uint64_t cur_offset = kWSnapHeaderSize;
    std::vector<WSnapSectionDescriptor> descriptors;
    descriptors.reserve(pending.size());

    // Calculate total buffer size
    std::size_t total_payload_bytes = 0;
    for (const auto& sec : pending) {
        total_payload_bytes += sec.payload.size();
    }
    const std::uint64_t dir_offset = cur_offset + total_payload_bytes;
    const std::uint64_t total_file_size = dir_offset + pending.size() * kWSnapSectionDescriptorSize;

    out_buffer.assign(static_cast<std::size_t>(total_file_size), 0u);
    std::uint8_t* raw_buf = out_buffer.data();

    // Write Payloads
    for (const auto& sec : pending) {
        const std::uint64_t sec_size = sec.payload.size();
        const std::uint32_t sec_crc = trace::calculate_crc32c(sec.payload.data(), static_cast<std::size_t>(sec_size));

        std::memcpy(raw_buf + cur_offset, sec.payload.data(), static_cast<std::size_t>(sec_size));

        WSnapSectionDescriptor desc{};
        desc.section_type = sec.type;
        desc.section_id = sec.id;
        desc.file_offset = cur_offset;
        desc.compressed_size = sec_size;
        desc.uncompressed_size = sec_size;
        desc.section_crc32c = sec_crc;
        desc.flags = 0;
        desc.reserved = 0;
        descriptors.push_back(desc);

        cur_offset += sec_size;
    }

    // Write Section Directory Table
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        encode_section_descriptor(descriptors[i], raw_buf + dir_offset + i * kWSnapSectionDescriptorSize);
    }

    // Write Header
    WSnapHeader header{};
    header.magic = kWSnapMagic;
    header.version = kWSnapCurrentVersion;
    header.core_count = total_cores;
    header.compression_type = SnapshotCompressionType::None;
    header.flags = 0;
    header.timestamp_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    header.memory_capacity = (context.memory && options.include_memory) ? context.memory->size() : 0;
    header.section_dir_offset = dir_offset;
    header.section_count = static_cast<std::uint32_t>(descriptors.size());
    header.total_file_size = static_cast<std::uint32_t>(total_file_size);
    header.reserved0 = 0;
    header.reserved1 = 0;
    header.reserved2 = 0;

    encode_wsnap_header(header, raw_buf);

    return SnapshotResult::ok();
}

SnapshotResult SnapshotManager::capture_to_file(
    const SnapshotRestoreContext& context,
    const std::filesystem::path& path,
    const SnapshotCaptureOptions& options) {

    std::vector<std::uint8_t> buffer;
    const auto res = capture_to_buffer(context, buffer, options);
    if (!res.success) {
        return res;
    }

    const auto tmp_path = path.string() + ".tmp";
    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Failed to create file: " + tmp_path);
        }
        if (!file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size())) {
            return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Failed to write snapshot payload");
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Atomic rename failed: " + ec.message());
    }

    return SnapshotResult::ok();
}

SnapshotResult SnapshotManager::load_from_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& out_buffer) {

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SnapshotResult::fail(SnapshotErrorCode::FileNotFound, "Could not open file: " + path.string());
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(kWSnapHeaderSize)) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "File size smaller than header");
    }

    file.seekg(0, std::ios::beg);
    out_buffer.resize(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(out_buffer.data()), file_size)) {
        return SnapshotResult::fail(SnapshotErrorCode::ReadError, "Failed to read file contents");
    }

    return SnapshotResult::ok();
}

SnapshotResult SnapshotManager::restore_from_file(
    const std::filesystem::path& path,
    SnapshotRestoreContext& context) {

    std::vector<std::uint8_t> buffer;
    const auto load_res = load_from_file(path, buffer);
    if (!load_res.success) {
        return load_res;
    }

    return restore_snapshot_from_buffer(buffer.data(), buffer.size(), context);
}

SnapshotValidationResult SnapshotManager::validate_file(const std::filesystem::path& path) {
    return validate_snapshot_file(path);
}

SnapshotResult SnapshotManager::inspect_file(
    const std::filesystem::path& path,
    SnapshotInspection& out_inspection) {

    std::vector<std::uint8_t> buffer;
    const auto load_res = load_from_file(path, buffer);
    if (!load_res.success) {
        return load_res;
    }

    const auto val_res = validate_snapshot_buffer(buffer.data(), buffer.size());
    if (!val_res.valid) {
        return SnapshotResult::fail(val_res.error_code, val_res.error_message);
    }

    const auto hdr_res = decode_wsnap_header(buffer.data(), buffer.size(), out_inspection.header);
    if (!hdr_res.success) {
        return hdr_res;
    }

    const auto dir_res = parse_and_validate_directory(buffer.data(), buffer.size(), out_inspection.header, out_inspection.sections);
    if (!dir_res.success) {
        return dir_res;
    }

    // Search for metadata
    for (const auto& sec : out_inspection.sections) {
        if (sec.section_type == SnapshotSectionType::Metadata) {
            (void)deserialize_metadata(buffer.data() + sec.file_offset, static_cast<std::size_t>(sec.compressed_size),
                                       out_inspection.metadata_description, out_inspection.metadata_creator);
            break;
        }
    }

    std::ostringstream ss;
    ss << "=== WinIOS Architectural Snapshot Inspection ===\n"
       << "File: " << path.string() << "\n"
       << "File Size: " << buffer.size() << " bytes\n"
       << "Version: " << out_inspection.header.version << "\n"
       << "Cores: " << out_inspection.header.core_count << "\n"
       << "Guest Memory Capacity: " << out_inspection.header.memory_capacity << " bytes ("
       << (out_inspection.header.memory_capacity / (1024 * 1024)) << " MB)\n"
       << "Section Count: " << out_inspection.sections.size() << "\n"
       << "Description: " << out_inspection.metadata_description << "\n"
       << "Creator: " << out_inspection.metadata_creator << "\n"
       << "Header CRC32C: 0x" << std::hex << out_inspection.header.header_crc32c << std::dec << "\n\n"
       << "--- Sections Directory ---\n";

    for (std::size_t i = 0; i < out_inspection.sections.size(); ++i) {
        const auto& s = out_inspection.sections[i];
        ss << "  [" << std::setw(2) << i << "] "
           << std::setw(14) << section_type_name(s.section_type)
           << " (id: " << s.section_id << ")"
           << " offset: 0x" << std::hex << s.file_offset << std::dec
           << " size: " << s.compressed_size << " bytes"
           << " CRC: 0x" << std::hex << s.section_crc32c << std::dec << "\n";
    }

    out_inspection.formatted_summary = ss.str();
    return SnapshotResult::ok();
}

} // namespace winios::snapshot
