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
//
// Stage 39 layout: the historical 1080-byte prefix is preserved byte-for-byte;
// a 112-byte architectural-debug block is appended.  Deserialization accepts
// the prefix alone (older snapshots) and ignores the debug block when absent.
inline constexpr std::size_t kCoreArchLegacyPayloadSize = 1080;
inline constexpr std::size_t kCoreArchDebugBlockSize = 144;
inline constexpr std::size_t kCoreArchPmuBlockSize = 96;
inline constexpr std::size_t kCoreArchPayloadSize =
    kCoreArchLegacyPayloadSize + kCoreArchDebugBlockSize + kCoreArchPmuBlockSize; // 1320
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

// 9. Virtual Device State
std::vector<std::uint8_t> serialize_device_state(std::uint32_t device_id, const std::vector<std::uint8_t>& device_payload);
[[nodiscard]] SnapshotResult deserialize_device_state(const std::uint8_t* src, std::size_t size,
                                                      std::uint32_t& out_device_id,
                                                      std::vector<std::uint8_t>& out_payload);

// Cluster Header Decode Routine (strictly version 2)
[[nodiscard]] SnapshotResult decode_smp_wsnap_header(const std::uint8_t* src, std::size_t size,
                                                     WSnapHeader& out_header) noexcept;

} // namespace winios::snapshot
