#include "snapshot/smp_snapshot_validation.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/snapshot_validation.h"
#include "snapshot/wsnap_format.h"
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

SnapshotValidationResult validate_smp_snapshot_buffer(const std::uint8_t* data, std::size_t size) noexcept {
    std::vector<std::string> diagnostics;

    if (!data || size < kWSnapHeaderSize) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for WSnap header");
    }

    WSnapHeader header{};
    const auto hdr_res = decode_smp_wsnap_header(data, size, header);
    if (!hdr_res.success) {
        return SnapshotValidationResult::fail(hdr_res.error_code, hdr_res.error_message);
    }

    if (header.core_count == 0 || header.core_count > 128) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidStateData,
            "Invalid cluster core count: " + std::to_string(header.core_count));
    }

    if (header.memory_capacity == 0) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MemoryCapacityMismatch,
            "Invalid cluster memory capacity: 0 bytes");
    }

    diagnostics.push_back("Cluster Header OK: version=" + std::to_string(header.version) +
                          ", cores=" + std::to_string(header.core_count) +
                          ", mem_capacity=" + std::to_string(header.memory_capacity) +
                          ", sections=" + std::to_string(header.section_count));

    std::vector<WSnapSectionDescriptor> descriptors;
    const auto dir_res = parse_and_validate_directory(data, size, header, descriptors);
    if (!dir_res.success) {
        return SnapshotValidationResult::fail(dir_res.error_code, dir_res.error_message, diagnostics);
    }

    // Required cluster section tracking
    bool has_core_directory = false;
    bool has_shared_memory = false;
    bool has_scheduler = false;
    bool has_exclusive_monitor = false;
    bool has_interrupt_router = false;
    bool has_integrity = false;
    std::vector<bool> has_core_state(header.core_count, false);

    const WSnapSectionDescriptor* core_dir_desc = nullptr;
    const WSnapSectionDescriptor* shared_mem_desc = nullptr;
    const WSnapSectionDescriptor* sched_desc = nullptr;
    const WSnapSectionDescriptor* monitor_desc = nullptr;
    const WSnapSectionDescriptor* irq_desc = nullptr;
    const WSnapSectionDescriptor* integrity_desc = nullptr;

    for (const auto& d : descriptors) {
        switch (d.section_type) {
        case SnapshotSectionType::CoreDirectory:
            has_core_directory = true;
            core_dir_desc = &d;
            break;
        case SnapshotSectionType::SharedMemory:
            has_shared_memory = true;
            shared_mem_desc = &d;
            break;
        case SnapshotSectionType::Scheduler:
            has_scheduler = true;
            sched_desc = &d;
            break;
        case SnapshotSectionType::ExclusiveMonitor:
            has_exclusive_monitor = true;
            monitor_desc = &d;
            break;
        case SnapshotSectionType::InterruptRouter:
            has_interrupt_router = true;
            irq_desc = &d;
            break;
        case SnapshotSectionType::Integrity:
            has_integrity = true;
            integrity_desc = &d;
            break;
        case SnapshotSectionType::CoreArchitecturalState:
            if (d.section_id < header.core_count) {
                has_core_state[d.section_id] = true;
            }
            break;
        default:
            break;
        }
        diagnostics.push_back("Section verified: " + std::string(section_type_name(d.section_type)) +
                              " id=" + std::to_string(d.section_id) +
                              " size=" + std::to_string(d.compressed_size) +
                              " crc=0x" + std::to_string(d.section_crc32c));
    }

    if (!has_core_directory) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required CORE_DIRECTORY section", diagnostics);
    }
    if (!has_shared_memory) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required SHARED_MEMORY section", diagnostics);
    }
    if (!has_scheduler) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required SCHEDULER section", diagnostics);
    }
    if (!has_exclusive_monitor) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required EXCLUSIVE_MONITOR section", diagnostics);
    }
    if (!has_interrupt_router) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required INTERRUPT_ROUTER section", diagnostics);
    }
    if (!has_integrity) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
            "Cluster snapshot is missing required INTEGRITY section", diagnostics);
    }

    for (std::size_t c = 0; c < header.core_count; ++c) {
        if (!has_core_state[c]) {
            return SnapshotValidationResult::fail(SnapshotErrorCode::MissingRequiredSection,
                "Cluster snapshot is missing CORE_ARCHITECTURAL_STATE for core " + std::to_string(c), diagnostics);
        }
    }

    // Semantic checks
    if (core_dir_desc) {
        std::vector<CoreDirectoryEntry> entries;
        const auto res = deserialize_core_directory(data + core_dir_desc->file_offset,
                                                    static_cast<std::size_t>(core_dir_desc->compressed_size),
                                                    entries);
        if (!res.success) {
            return SnapshotValidationResult::fail(res.error_code, "CoreDirectory corrupted: " + res.error_message, diagnostics);
        }
        if (entries.size() != header.core_count) {
            return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidStateData,
                "CoreDirectory count (" + std::to_string(entries.size()) +
                ") does not match header core count (" + std::to_string(header.core_count) + ")", diagnostics);
        }
    }

    if (sched_desc) {
        SchedulerSnapshotData sched_data;
        const auto res = deserialize_scheduler_state(data + sched_desc->file_offset,
                                                     static_cast<std::size_t>(sched_desc->compressed_size),
                                                     sched_data);
        if (!res.success) {
            return SnapshotValidationResult::fail(res.error_code, "Scheduler state corrupted: " + res.error_message, diagnostics);
        }
        if (sched_data.registered_core_count != header.core_count) {
            return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidStateData,
                "Scheduler registered core count mismatch", diagnostics);
        }
        if (sched_data.registered_core_count > 0 && sched_data.current_core_index >= sched_data.registered_core_count) {
            return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidStateData,
                "Scheduler current_core_index exceeds registered core count", diagnostics);
        }
    }

    if (integrity_desc) {
        IntegritySnapshotData integ_data;
        const auto res = deserialize_integrity_section(data + integrity_desc->file_offset,
                                                       static_cast<std::size_t>(integrity_desc->compressed_size),
                                                       integ_data);
        if (!res.success) {
            return SnapshotValidationResult::fail(res.error_code, "Integrity section corrupted: " + res.error_message, diagnostics);
        }
        // Verify section CRC table matches descriptors
        for (const auto& sc : integ_data.section_crcs) {
            bool found = false;
            for (const auto& d : descriptors) {
                if (static_cast<std::uint32_t>(d.section_type) == sc.section_type && d.section_id == sc.section_id) {
                    found = true;
                    if (d.section_crc32c != sc.crc32c) {
                        return SnapshotValidationResult::fail(SnapshotErrorCode::SectionCrcMismatch,
                            "Integrity section CRC record mismatch for section type " + std::to_string(sc.section_type), diagnostics);
                    }
                    break;
                }
            }
            if (!found) {
                return SnapshotValidationResult::fail(SnapshotErrorCode::InvalidSectionDirectory,
                    "Integrity references non-existent section in directory", diagnostics);
            }
        }
    }

    return SnapshotValidationResult::ok();
}

SnapshotValidationResult validate_smp_snapshot_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::FileNotFound, "Could not open file: " + path.string());
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(kWSnapHeaderSize)) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::TruncatedFile, "File size smaller than WSnap header");
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return SnapshotValidationResult::fail(SnapshotErrorCode::ReadError, "Failed to read file contents");
    }

    return validate_smp_snapshot_buffer(buffer.data(), buffer.size());
}

} // namespace winios::snapshot
