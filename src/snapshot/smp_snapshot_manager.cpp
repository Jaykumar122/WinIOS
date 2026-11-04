#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/smp_snapshot_validation.h"
#include "snapshot/snapshot_validation.h"
#include "trace/trace_serializer.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::snapshot {

namespace {

struct PendingClusterSection {
    SnapshotSectionType type;
    std::uint32_t id;
    std::vector<std::uint8_t> payload;
};

} // namespace

SnapshotResult SmpSnapshotManager::capture_to_buffer(
    const SmpScheduler& scheduler,
    std::vector<std::uint8_t>& out_buffer,
    const SmpSnapshotCaptureOptions& options) {

    const std::size_t core_count = scheduler.core_count();
    if (core_count == 0) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidArgument, "Cannot capture empty cluster with 0 registered cores");
    }

    std::vector<PendingClusterSection> pending;

    // 1. Metadata
    pending.push_back({
        SnapshotSectionType::Metadata,
        0,
        serialize_cluster_metadata(options.description, options.creator, options.cluster_topology)
    });

    // 2. Core Directory
    std::vector<CoreDirectoryEntry> dir_entries;
    dir_entries.reserve(core_count);
    for (std::size_t c = 0; c < core_count; ++c) {
        const auto& slot = scheduler.get_slot(c);
        CoreDirectoryEntry entry;
        entry.core_id = static_cast<std::uint32_t>(c);
        entry.lifecycle = slot.state.lifecycle;
        entry.active = slot.active;
        dir_entries.push_back(entry);
    }
    pending.push_back({SnapshotSectionType::CoreDirectory, 0, serialize_core_directory(dir_entries)});

    // 3. Per-Core Architectural States
    for (std::size_t c = 0; c < core_count; ++c) {
        const auto& slot = scheduler.get_slot(c);
        SmpCoreState core_state = slot.state;
        if (slot.cpu) {
            core_state.capture_from_cpu(*slot.cpu);
        }
        pending.push_back({
            SnapshotSectionType::CoreArchitecturalState,
            static_cast<std::uint32_t>(c),
            serialize_core_architectural_state(core_state)
        });
    }

    // 4. Shared Memory (Single sparse serialization)
    pending.push_back({
        SnapshotSectionType::SharedMemory,
        0,
        serialize_shared_memory_state(scheduler.shared_memory().raw_memory(), options.chunk_size, options.omit_zero_chunks)
    });

    // 5. Scheduler Coordinator State
    SchedulerSnapshotData sched_data;
    sched_data.quantum_instructions = scheduler.quantum_instructions();
    sched_data.global_retirement_sequence = scheduler.global_retirement_sequence();
    sched_data.registered_core_count = static_cast<std::uint32_t>(core_count);
    sched_data.current_core_index = static_cast<std::uint32_t>(scheduler.current_core_index());
    sched_data.slot_metrics.reserve(core_count);
    for (std::size_t c = 0; c < core_count; ++c) {
        const auto& slot = scheduler.get_slot(c);
        SchedulerSnapshotData::SlotMetrics sm;
        sm.core_id = static_cast<std::uint32_t>(c);
        sm.active = slot.active;
        sm.total_instructions_retired = slot.total_instructions_retired;
        sm.quantums_allocated = slot.quantums_allocated;
        sm.early_truncations = slot.early_truncations;
        sched_data.slot_metrics.push_back(sm);
    }
    pending.push_back({SnapshotSectionType::Scheduler, 0, serialize_scheduler_state(sched_data)});

    // 6. Exclusive Monitor State
    ExclusiveMonitorSnapshotData monitor_data;
    monitor_data.max_cores = static_cast<std::uint32_t>(scheduler.exclusive_monitor().max_cores());
    monitor_data.reservations = scheduler.exclusive_monitor().all_reservations();
    pending.push_back({SnapshotSectionType::ExclusiveMonitor, 0, serialize_exclusive_monitor_state(monitor_data)});

    // 7. Cross-Core Interrupt Router State
    InterruptRouterSnapshotData irq_data;
    irq_data.max_cores = static_cast<std::uint32_t>(scheduler.interrupt_router().max_cores());
    irq_data.next_sequence = scheduler.interrupt_router().next_sequence();
    irq_data.core_queues.resize(irq_data.max_cores);
    const auto& all_q = scheduler.interrupt_router().all_queues();
    for (std::size_t c = 0; c < all_q.size(); ++c) {
        irq_data.core_queues[c].assign(all_q[c].begin(), all_q[c].end());
    }
    pending.push_back({SnapshotSectionType::InterruptRouter, 0, serialize_interrupt_router_state(irq_data)});

    // 8. Integrity Section
    IntegritySnapshotData integ_data;
    integ_data.canonical_cluster_hash = scheduler.compute_canonical_multicore_hash();
    integ_data.timestamp_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    integ_data.section_crcs.reserve(pending.size());
    for (const auto& sec : pending) {
        const std::uint32_t crc = trace::calculate_crc32c(sec.payload.data(), sec.payload.size());
        integ_data.section_crcs.push_back({
            static_cast<std::uint32_t>(sec.type),
            sec.id,
            crc
        });
    }
    pending.push_back({SnapshotSectionType::Integrity, 0, serialize_integrity_section(integ_data)});

    // Layout the file:
    // [Header (64 bytes)]
    // [Payloads...]
    // [Section Directory Table (section_count * 40 bytes)]

    std::uint64_t cur_offset = kWSnapHeaderSize;
    std::size_t total_payload_bytes = 0;
    for (const auto& sec : pending) {
        total_payload_bytes += sec.payload.size();
    }

    const std::uint64_t dir_offset = cur_offset + total_payload_bytes;
    const std::uint64_t total_file_size = dir_offset + pending.size() * kWSnapSectionDescriptorSize;

    out_buffer.assign(static_cast<std::size_t>(total_file_size), 0u);
    std::uint8_t* raw_buf = out_buffer.data();

    std::vector<WSnapSectionDescriptor> descriptors;
    descriptors.reserve(pending.size());

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
    header.version = kWSnapVersion2;
    header.core_count = static_cast<std::uint16_t>(core_count);
    header.compression_type = SnapshotCompressionType::None;
    header.flags = 0;
    header.timestamp_ns = integ_data.timestamp_ns;
    header.memory_capacity = scheduler.shared_memory().size();
    header.section_dir_offset = dir_offset;
    header.section_count = static_cast<std::uint32_t>(descriptors.size());
    header.total_file_size = static_cast<std::uint32_t>(total_file_size);
    header.reserved0 = 0;
    header.reserved1 = 0;
    header.reserved2 = 0;

    encode_wsnap_header(header, raw_buf);

    return SnapshotResult::ok();
}

SnapshotResult SmpSnapshotManager::capture_to_file(
    const SmpScheduler& scheduler,
    const std::filesystem::path& path,
    const SmpSnapshotCaptureOptions& options) {

    std::vector<std::uint8_t> buffer;
    const auto cap_res = capture_to_buffer(scheduler, buffer, options);
    if (!cap_res.success) {
        return cap_res;
    }

    const std::filesystem::path tmp_path = path.string() + ".tmp";
    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Could not open temporary file for writing: " + tmp_path.string());
        }
        if (!file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()))) {
            file.close();
            std::filesystem::remove(tmp_path);
            return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Failed to write buffer to temporary file: " + tmp_path.string());
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        return SnapshotResult::fail(SnapshotErrorCode::WriteError, "Atomic rename failed: " + ec.message());
    }

    return SnapshotResult::ok();
}

SnapshotValidationResult SmpSnapshotManager::validate_buffer(
    const std::uint8_t* data,
    std::size_t size) noexcept {
    return validate_smp_snapshot_buffer(data, size);
}

SnapshotValidationResult SmpSnapshotManager::validate_file(
    const std::filesystem::path& path) {
    return validate_smp_snapshot_file(path);
}

SnapshotResult SmpSnapshotManager::restore_from_buffer(
    const std::uint8_t* data,
    std::size_t size,
    SmpScheduler& scheduler) {

    // 1. Pre-flight full validation
    const auto val_res = validate_smp_snapshot_buffer(data, size);
    if (!val_res.valid) {
        return SnapshotResult::fail(val_res.error_code, "Snapshot validation failed before restore: " + val_res.error_message);
    }

    WSnapHeader header{};
    (void)decode_smp_wsnap_header(data, size, header);

    if (scheduler.core_count() != header.core_count) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidStateData,
            "Target scheduler core count (" + std::to_string(scheduler.core_count()) +
            ") does not match snapshot core count (" + std::to_string(header.core_count) + ")");
    }

    if (scheduler.shared_memory().size() != header.memory_capacity) {
        return SnapshotResult::fail(SnapshotErrorCode::MemoryCapacityMismatch,
            "Target memory size (" + std::to_string(scheduler.shared_memory().size()) +
            ") does not match snapshot memory capacity (" + std::to_string(header.memory_capacity) + ")");
    }

    std::vector<WSnapSectionDescriptor> descriptors;
    (void)parse_and_validate_directory(data, size, header, descriptors);

    // 2. Staging Decode Phase (no mutation of live state if any decode fails)
    std::vector<CoreDirectoryEntry> core_dir;
    std::vector<SmpCoreState> core_states(header.core_count);
    SchedulerSnapshotData sched_data;
    ExclusiveMonitorSnapshotData monitor_data;
    InterruptRouterSnapshotData irq_data;
    IntegritySnapshotData integrity_data;
    const WSnapSectionDescriptor* mem_desc = nullptr;

    for (const auto& d : descriptors) {
        const std::uint8_t* sec_data = data + d.file_offset;
        const std::size_t sec_size = static_cast<std::size_t>(d.compressed_size);

        switch (d.section_type) {
        case SnapshotSectionType::CoreDirectory: {
            auto res = deserialize_core_directory(sec_data, sec_size, core_dir);
            if (!res.success) return res;
            break;
        }
        case SnapshotSectionType::CoreArchitecturalState: {
            if (d.section_id < header.core_count) {
                auto res = deserialize_core_architectural_state(sec_data, sec_size, core_states[d.section_id]);
                if (!res.success) return res;
            }
            break;
        }
        case SnapshotSectionType::SharedMemory:
            mem_desc = &d;
            break;
        case SnapshotSectionType::Scheduler: {
            auto res = deserialize_scheduler_state(sec_data, sec_size, sched_data);
            if (!res.success) return res;
            break;
        }
        case SnapshotSectionType::ExclusiveMonitor: {
            auto res = deserialize_exclusive_monitor_state(sec_data, sec_size, monitor_data);
            if (!res.success) return res;
            break;
        }
        case SnapshotSectionType::InterruptRouter: {
            auto res = deserialize_interrupt_router_state(sec_data, sec_size, irq_data);
            if (!res.success) return res;
            break;
        }
        case SnapshotSectionType::Integrity: {
            auto res = deserialize_integrity_section(sec_data, sec_size, integrity_data);
            if (!res.success) return res;
            break;
        }
        default:
            break;
        }
    }

    if (!mem_desc) {
        return SnapshotResult::fail(SnapshotErrorCode::MissingRequiredSection, "SharedMemory section missing");
    }

    // 3. Atomic Commit Phase
    // Restore shared memory
    {
        auto res = deserialize_shared_memory_state(data + mem_desc->file_offset,
                                                   static_cast<std::size_t>(mem_desc->compressed_size),
                                                   scheduler.shared_memory().raw_memory());
        if (!res.success) return res;
    }

    // Restore per-core architectural states and metrics
    for (std::size_t c = 0; c < header.core_count; ++c) {
        auto& slot = scheduler.get_slot(c);
        slot.state = core_states[c];
        if (c < sched_data.slot_metrics.size()) {
            slot.total_instructions_retired = sched_data.slot_metrics[c].total_instructions_retired;
            slot.quantums_allocated = sched_data.slot_metrics[c].quantums_allocated;
            slot.early_truncations = sched_data.slot_metrics[c].early_truncations;
        }
        if (slot.cpu) {
            slot.state.apply_to_cpu(*slot.cpu);
        }
    }

    // Restore exclusive monitor reservations
    scheduler.exclusive_monitor().reset();
    for (const auto& r : monitor_data.reservations) {
        scheduler.exclusive_monitor().set_core_reservation(r);
    }

    // Restore interrupt router
    scheduler.interrupt_router().clear_all();
    scheduler.interrupt_router().set_next_sequence(irq_data.next_sequence);
    for (std::size_t c = 0; c < irq_data.core_queues.size(); ++c) {
        scheduler.interrupt_router().restore_queue(c, irq_data.core_queues[c]);
    }

    // Restore scheduler coordinator
    scheduler.set_quantum_instructions(sched_data.quantum_instructions);
    scheduler.set_global_retirement_sequence(sched_data.global_retirement_sequence);
    scheduler.set_current_core_index(sched_data.current_core_index);

    return SnapshotResult::ok();
}

SnapshotResult SmpSnapshotManager::restore_from_file(
    const std::filesystem::path& path,
    SmpScheduler& scheduler) {

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SnapshotResult::fail(SnapshotErrorCode::FileNotFound, "Could not open file: " + path.string());
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(kWSnapHeaderSize)) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "File smaller than WSnap header");
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return SnapshotResult::fail(SnapshotErrorCode::ReadError, "Failed reading snapshot file: " + path.string());
    }

    return restore_from_buffer(buffer.data(), buffer.size(), scheduler);
}

SnapshotResult SmpSnapshotManager::inspect_buffer(
    const std::uint8_t* data,
    std::size_t size,
    SmpSnapshotInspection& out_inspection) {

    const auto val_res = validate_smp_snapshot_buffer(data, size);
    if (!val_res.valid) {
        return SnapshotResult::fail(val_res.error_code, val_res.error_message);
    }

    (void)decode_smp_wsnap_header(data, size, out_inspection.header);
    (void)parse_and_validate_directory(data, size, out_inspection.header, out_inspection.sections);

    for (const auto& sec : out_inspection.sections) {
        const std::uint8_t* p = data + sec.file_offset;
        const std::size_t sz = static_cast<std::size_t>(sec.compressed_size);

        switch (sec.section_type) {
        case SnapshotSectionType::Metadata:
            (void)deserialize_cluster_metadata(p, sz, out_inspection.description, out_inspection.creator, out_inspection.topology);
            break;
        case SnapshotSectionType::CoreDirectory:
            (void)deserialize_core_directory(p, sz, out_inspection.core_directory);
            break;
        case SnapshotSectionType::Scheduler:
            (void)deserialize_scheduler_state(p, sz, out_inspection.scheduler_data);
            break;
        case SnapshotSectionType::ExclusiveMonitor:
            (void)deserialize_exclusive_monitor_state(p, sz, out_inspection.exclusive_monitor_data);
            break;
        case SnapshotSectionType::InterruptRouter:
            (void)deserialize_interrupt_router_state(p, sz, out_inspection.interrupt_router_data);
            break;
        case SnapshotSectionType::Integrity:
            (void)deserialize_integrity_section(p, sz, out_inspection.integrity_data);
            break;
        default:
            break;
        }
    }

    std::ostringstream ss;
    ss << "=== WinIOS Multi-Core Cluster Architectural Snapshot Inspection ===\n"
       << "Format Version: " << out_inspection.header.version << " (.wsnap v2)\n"
       << "Cluster Cores: " << out_inspection.header.core_count << "\n"
       << "Shared Guest Memory: " << out_inspection.header.memory_capacity << " bytes ("
       << (out_inspection.header.memory_capacity / (1024 * 1024)) << " MB)\n"
       << "Description: " << out_inspection.description << "\n"
       << "Creator: " << out_inspection.creator << "\n"
       << "Topology: " << out_inspection.topology << "\n"
       << "Canonical Multicore Hash: 0x" << std::hex << out_inspection.integrity_data.canonical_cluster_hash << std::dec << "\n"
       << "Scheduler Quantum: " << out_inspection.scheduler_data.quantum_instructions << " instrs\n"
       << "Global Retirement Sequence: " << out_inspection.scheduler_data.global_retirement_sequence << "\n"
       << "Section Count: " << out_inspection.sections.size() << "\n\n"
       << "--- Cluster Core Directory ---\n";

    for (const auto& entry : out_inspection.core_directory) {
        ss << "  Core " << entry.core_id << ": " << core_lifecycle_state_name(entry.lifecycle)
           << (entry.active ? " [ACTIVE]" : " [INACTIVE]") << "\n";
    }

    ss << "\n--- Section Directory ---\n";
    for (std::size_t i = 0; i < out_inspection.sections.size(); ++i) {
        const auto& s = out_inspection.sections[i];
        ss << "  [" << std::setw(2) << i << "] "
           << std::setw(24) << section_type_name(s.section_type)
           << " (id: " << s.section_id << ")"
           << " offset: 0x" << std::hex << s.file_offset << std::dec
           << " size: " << s.compressed_size << " bytes"
           << " CRC: 0x" << std::hex << s.section_crc32c << std::dec << "\n";
    }

    out_inspection.formatted_summary = ss.str();
    return SnapshotResult::ok();
}

SnapshotResult SmpSnapshotManager::inspect_file(
    const std::filesystem::path& path,
    SmpSnapshotInspection& out_inspection) {

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SnapshotResult::fail(SnapshotErrorCode::FileNotFound, "Could not open file: " + path.string());
    }

    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(kWSnapHeaderSize)) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "File smaller than WSnap header");
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return SnapshotResult::fail(SnapshotErrorCode::ReadError, "Failed reading snapshot file: " + path.string());
    }

    return inspect_buffer(buffer.data(), buffer.size(), out_inspection);
}

SnapshotResult SmpSnapshotManager::compare_snapshots(
    const std::filesystem::path& path1,
    const std::filesystem::path& path2,
    std::string& out_diff_report) {

    SmpSnapshotInspection i1{};
    SmpSnapshotInspection i2{};

    const auto res1 = inspect_file(path1, i1);
    if (!res1.success) return res1;
    const auto res2 = inspect_file(path2, i2);
    if (!res2.success) return res2;

    std::ostringstream ss;
    ss << "=== Multi-Core Snapshot Differential Analysis ===\n"
       << "File 1: " << path1.string() << "\n"
       << "File 2: " << path2.string() << "\n\n";

    bool identical = true;

    if (i1.header.core_count != i2.header.core_count) {
        ss << "[DIFF] Core count differs: " << i1.header.core_count << " vs " << i2.header.core_count << "\n";
        identical = false;
    }
    if (i1.header.memory_capacity != i2.header.memory_capacity) {
        ss << "[DIFF] Memory capacity differs: " << i1.header.memory_capacity << " vs " << i2.header.memory_capacity << "\n";
        identical = false;
    }
    if (i1.integrity_data.canonical_cluster_hash != i2.integrity_data.canonical_cluster_hash) {
        ss << "[DIFF] Canonical cluster hash differs: 0x" << std::hex
           << i1.integrity_data.canonical_cluster_hash << " vs 0x"
           << i2.integrity_data.canonical_cluster_hash << std::dec << "\n";
        identical = false;
    }
    if (i1.scheduler_data.global_retirement_sequence != i2.scheduler_data.global_retirement_sequence) {
        ss << "[DIFF] Global retirement sequence differs: "
           << i1.scheduler_data.global_retirement_sequence << " vs "
           << i2.scheduler_data.global_retirement_sequence << "\n";
        identical = false;
    }
    if (i1.sections.size() != i2.sections.size()) {
        ss << "[DIFF] Section count differs: " << i1.sections.size() << " vs " << i2.sections.size() << "\n";
        identical = false;
    }

    if (identical) {
        ss << "Result: Cluster snapshots are ARCHITECTURALLY IDENTICAL.\n";
    } else {
        ss << "Result: Cluster snapshots HAVE DIVERGENT ARCHITECTURAL STATE.\n";
    }

    out_diff_report = ss.str();
    return SnapshotResult::ok();
}

} // namespace winios::snapshot
