#pragma once

#include "snapshot/snapshot_types.h"
#include "snapshot/wsnap_format.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_exclusive_monitor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::snapshot {

inline constexpr std::size_t kDefaultSparseChunkSizeBytes = 4096; // 4 KB sparse chunking

struct SmpSnapshotCaptureOptions {
    std::string description{"WinIOS Multi-Core Cluster Snapshot"};
    std::string creator{"WinIOS SmpSnapshotEngine"};
    std::string cluster_topology{"Standard Symmetric Multi-Processing Cluster"};
    std::size_t chunk_size{kDefaultSparseChunkSizeBytes};
    bool omit_zero_chunks{true};
    bool verify_hash_on_capture{true};
};

struct CoreDirectoryEntry {
    std::uint32_t core_id{0};
    CoreLifecycleState lifecycle{CoreLifecycleState::Halted};
    bool active{false};
};

struct SchedulerSnapshotData {
    std::uint64_t quantum_instructions{0};
    std::uint64_t global_retirement_sequence{0};
    std::uint32_t registered_core_count{0};
    std::uint32_t current_core_index{0};

    struct SlotMetrics {
        std::uint32_t core_id{0};
        bool active{false};
        std::uint64_t total_instructions_retired{0};
        std::uint64_t quantums_allocated{0};
        std::uint64_t early_truncations{0};
    };
    std::vector<SlotMetrics> slot_metrics;
};

struct ExclusiveMonitorSnapshotData {
    std::uint32_t max_cores{0};
    std::vector<ReservationEntry> reservations;
};

struct InterruptRouterSnapshotData {
    std::uint32_t max_cores{0};
    std::uint64_t next_sequence{1};
    std::vector<std::vector<InterruptRequest>> core_queues;
};

struct IntegritySnapshotData {
    std::uint64_t canonical_cluster_hash{0};
    std::uint64_t timestamp_ns{0};
    struct SectionCrcEntry {
        std::uint32_t section_type{0};
        std::uint32_t section_id{0};
        std::uint32_t crc32c{0};
    };
    std::vector<SectionCrcEntry> section_crcs;
};

struct SmpSnapshotInspection {
    WSnapHeader header{};
    std::vector<WSnapSectionDescriptor> sections{};
    std::string description;
    std::string creator;
    std::string topology;
    std::vector<CoreDirectoryEntry> core_directory;
    SchedulerSnapshotData scheduler_data;
    ExclusiveMonitorSnapshotData exclusive_monitor_data;
    InterruptRouterSnapshotData interrupt_router_data;
    IntegritySnapshotData integrity_data;
    std::string formatted_summary;
};

} // namespace winios::snapshot
