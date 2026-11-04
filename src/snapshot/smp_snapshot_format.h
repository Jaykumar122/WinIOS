#pragma once

#include "snapshot/smp_snapshot_types.h"
#include "snapshot/snapshot_types.h"
#include "memory/memory.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace winios::snapshot {

// 1. Core Directory
std::vector<std::uint8_t> serialize_core_directory(const std::vector<CoreDirectoryEntry>& entries);
[[nodiscard]] SnapshotResult deserialize_core_directory(const std::uint8_t* src, std::size_t size,
                                                        std::vector<CoreDirectoryEntry>& out_entries);

// 2. Core Architectural State (Per-Core SmpCoreState)
std::vector<std::uint8_t> serialize_core_architectural_state(const SmpCoreState& state);
[[nodiscard]] SnapshotResult deserialize_core_architectural_state(const std::uint8_t* src, std::size_t size,
                                                                 SmpCoreState& out_state);

// 3. Shared Memory (Single Sparse Instance)
std::vector<std::uint8_t> serialize_shared_memory_state(const Memory& mem,
                                                        std::size_t chunk_size = kDefaultSparseChunkSizeBytes,
                                                        bool omit_zero_chunks = true);
[[nodiscard]] SnapshotResult deserialize_shared_memory_state(const std::uint8_t* src, std::size_t size,
                                                            Memory& mem);

// 4. Scheduler Coordinator State
std::vector<std::uint8_t> serialize_scheduler_state(const SchedulerSnapshotData& data);
[[nodiscard]] SnapshotResult deserialize_scheduler_state(const std::uint8_t* src, std::size_t size,
                                                        SchedulerSnapshotData& out_data);

// 5. Exclusive Monitor State
std::vector<std::uint8_t> serialize_exclusive_monitor_state(const ExclusiveMonitorSnapshotData& data);
[[nodiscard]] SnapshotResult deserialize_exclusive_monitor_state(const std::uint8_t* src, std::size_t size,
                                                                ExclusiveMonitorSnapshotData& out_data);

// 6. Cross-Core Interrupt Router State
std::vector<std::uint8_t> serialize_interrupt_router_state(const InterruptRouterSnapshotData& data);
[[nodiscard]] SnapshotResult deserialize_interrupt_router_state(const std::uint8_t* src, std::size_t size,
                                                               InterruptRouterSnapshotData& out_data);

// 7. Integrity Section (Multicore Canonical Hash + Checksums)
std::vector<std::uint8_t> serialize_integrity_section(const IntegritySnapshotData& data);
[[nodiscard]] SnapshotResult deserialize_integrity_section(const std::uint8_t* src, std::size_t size,
                                                           IntegritySnapshotData& out_data);

// 8. Cluster Metadata
std::vector<std::uint8_t> serialize_cluster_metadata(const std::string& desc,
                                                    const std::string& creator,
                                                    const std::string& topology);
[[nodiscard]] SnapshotResult deserialize_cluster_metadata(const std::uint8_t* src, std::size_t size,
                                                         std::string& out_desc,
                                                         std::string& out_creator,
                                                         std::string& out_topology);

// Cluster Header Decode Routine (strictly version 2)
[[nodiscard]] SnapshotResult decode_smp_wsnap_header(const std::uint8_t* src, std::size_t size,
                                                     WSnapHeader& out_header) noexcept;

} // namespace winios::snapshot
