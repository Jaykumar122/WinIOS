#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "snapshot/snapshot_types.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::snapshot {

// Constants
inline constexpr std::size_t kMemoryChunkSizeBytes = 65536; // 64 KB chunks

// Serialization routines producing packed Little-Endian byte buffers
std::vector<std::uint8_t> serialize_cpu_state(const CPU& cpu);
[[nodiscard]] SnapshotResult deserialize_cpu_state(const std::uint8_t* src, std::size_t size, CPU& cpu);

std::vector<std::uint8_t> serialize_memory_state(const Memory& memory, std::size_t chunk_size = kMemoryChunkSizeBytes);
[[nodiscard]] SnapshotResult deserialize_memory_state(const std::uint8_t* src, std::size_t size, Memory& memory);

std::vector<std::uint8_t> serialize_timer_state(const VirtualTime& vt, std::size_t core_count);
[[nodiscard]] SnapshotResult deserialize_timer_state(const std::uint8_t* src, std::size_t size, VirtualTime& vt);

std::vector<std::uint8_t> serialize_gic_state(const GICv3& gic);
[[nodiscard]] SnapshotResult deserialize_gic_state(const std::uint8_t* src, std::size_t size, GICv3& gic);

std::vector<std::uint8_t> serialize_pmu_state(const PMU& pmu, std::size_t core_id);
[[nodiscard]] SnapshotResult deserialize_pmu_state(const std::uint8_t* src, std::size_t size, PMU& pmu);

std::vector<std::uint8_t> serialize_debug_state(const DebugArchitecture& dbg, std::size_t core_id);
[[nodiscard]] SnapshotResult deserialize_debug_state(const std::uint8_t* src, std::size_t size, DebugArchitecture& dbg);

std::vector<std::uint8_t> serialize_metadata(const std::string& description, const std::string& creator);
[[nodiscard]] SnapshotResult deserialize_metadata(const std::uint8_t* src, std::size_t size, std::string& out_description, std::string& out_creator);

} // namespace winios::snapshot
