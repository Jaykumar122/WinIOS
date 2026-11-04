#pragma once

#include "snapshot/snapshot_types.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace winios::snapshot {

// Validates a raw memory buffer containing a multi-core cluster snapshot (.wsnap v2)
SnapshotValidationResult validate_smp_snapshot_buffer(const std::uint8_t* data, std::size_t size) noexcept;

// Validates a cluster snapshot file on disk
SnapshotValidationResult validate_smp_snapshot_file(const std::filesystem::path& path);

} // namespace winios::snapshot
