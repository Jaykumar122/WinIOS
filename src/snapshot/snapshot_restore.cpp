#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_validation.h"
#include <map>

namespace winios::snapshot {

SnapshotResult restore_snapshot_from_buffer(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotRestoreContext& context) {

    // 1. Transactional Pre-Validation: Validate entire buffer before touching state
    const auto val_res = validate_snapshot_buffer(data, size);
    if (!val_res.valid) {
        return SnapshotResult::fail(val_res.error_code, "Pre-restore validation failed: " + val_res.error_message);
    }

    WSnapHeader header{};
    const auto hdr_res = decode_wsnap_header(data, size, header);
    if (!hdr_res.success) {
        return hdr_res;
    }

    std::vector<WSnapSectionDescriptor> descriptors;
    const auto dir_res = parse_and_validate_directory(data, size, header, descriptors);
    if (!dir_res.success) {
        return dir_res;
    }

    // Index descriptors by section type and section ID
    const WSnapSectionDescriptor* memory_desc = nullptr;
    const WSnapSectionDescriptor* timer_desc = nullptr;
    const WSnapSectionDescriptor* gic_desc = nullptr;
    std::map<std::uint32_t, const WSnapSectionDescriptor*> cpu_descs;
    std::map<std::uint32_t, const WSnapSectionDescriptor*> pmu_descs;
    std::map<std::uint32_t, const WSnapSectionDescriptor*> debug_descs;

    for (const auto& d : descriptors) {
        switch (d.section_type) {
        case SnapshotSectionType::MemoryState:
            memory_desc = &d;
            break;
        case SnapshotSectionType::TimerState:
            timer_desc = &d;
            break;
        case SnapshotSectionType::GicState:
            gic_desc = &d;
            break;
        case SnapshotSectionType::CpuState:
            cpu_descs[d.section_id] = &d;
            break;
        case SnapshotSectionType::PmuState:
            pmu_descs[d.section_id] = &d;
            break;
        case SnapshotSectionType::DebugState:
            debug_descs[d.section_id] = &d;
            break;
        default:
            break;
        }
    }

    // --- Step 1: Restore Memory ---
    if (context.memory && memory_desc) {
        const std::uint8_t* mem_payload = data + memory_desc->file_offset;
        const auto res = deserialize_memory_state(mem_payload, static_cast<std::size_t>(memory_desc->compressed_size), *context.memory);
        if (!res.success) {
            return SnapshotResult::fail(res.error_code, "Failed to restore guest memory: " + res.error_message);
        }
    }

    // --- Step 2: Restore GIC and Interrupt State ---
    if (context.gic && gic_desc) {
        const std::uint8_t* gic_payload = data + gic_desc->file_offset;
        const auto res = deserialize_gic_state(gic_payload, static_cast<std::size_t>(gic_desc->compressed_size), *context.gic);
        if (!res.success) {
            return SnapshotResult::fail(res.error_code, "Failed to restore GIC state: " + res.error_message);
        }
    }

    // --- Step 3: Restore Timers & VirtualTime ---
    if (context.virtual_time && timer_desc) {
        const std::uint8_t* timer_payload = data + timer_desc->file_offset;
        const auto res = deserialize_timer_state(timer_payload, static_cast<std::size_t>(timer_desc->compressed_size), *context.virtual_time);
        if (!res.success) {
            return SnapshotResult::fail(res.error_code, "Failed to restore timer state: " + res.error_message);
        }
    }

    // Helper to restore per-core state (CPU, PMU, Debug)
    auto restore_core = [&](CPU* cpu, std::uint32_t core_id) -> SnapshotResult {
        if (!cpu) return SnapshotResult::ok();

        // PMU
        auto pmu_it = pmu_descs.find(core_id);
        if (pmu_it != pmu_descs.end()) {
            const auto* d = pmu_it->second;
            const auto res = deserialize_pmu_state(data + d->file_offset, static_cast<std::size_t>(d->compressed_size), cpu->get_pmu());
            if (!res.success) {
                return SnapshotResult::fail(res.error_code, "Failed to restore PMU on core " + std::to_string(core_id));
            }
        }

        // Debug
        auto dbg_it = debug_descs.find(core_id);
        if (dbg_it != debug_descs.end()) {
            const auto* d = dbg_it->second;
            const auto res = deserialize_debug_state(data + d->file_offset, static_cast<std::size_t>(d->compressed_size), cpu->get_debug_arch());
            if (!res.success) {
                return SnapshotResult::fail(res.error_code, "Failed to restore Debug on core " + std::to_string(core_id));
            }
        }

        // CPU Core State
        auto cpu_it = cpu_descs.find(core_id);
        if (cpu_it != cpu_descs.end()) {
            const auto* d = cpu_it->second;
            const auto res = deserialize_cpu_state(data + d->file_offset, static_cast<std::size_t>(d->compressed_size), *cpu);
            if (!res.success) {
                return SnapshotResult::fail(res.error_code, "Failed to restore CPU on core " + std::to_string(core_id));
            }
        }

        return SnapshotResult::ok();
    };

    // --- Step 4: Restore Primary CPU (Core 0) ---
    if (context.primary_cpu) {
        const auto res = restore_core(context.primary_cpu, 0);
        if (!res.success) {
            return res;
        }
    }

    // --- Step 5: Restore Secondary CPUs (if provided) ---
    for (std::size_t i = 0; i < context.secondary_cpus.size(); ++i) {
        const auto core_id = static_cast<std::uint32_t>(i + 1);
        const auto res = restore_core(context.secondary_cpus[i], core_id);
        if (!res.success) {
            return res;
        }
    }

    return SnapshotResult::ok();
}

} // namespace winios::snapshot
