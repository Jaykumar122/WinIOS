#pragma once

#include "snapshot/snapshot_types.h"
#include "snapshot/wsnap_format.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace winios::snapshot {

// Validates a raw .wsnap memory buffer or file contents
[[nodiscard]] SnapshotValidationResult validate_snapshot_buffer(
    const std::uint8_t* data,
    std::size_t size
) noexcept;

// Validates a .wsnap file on disk
[[nodiscard]] SnapshotValidationResult validate_snapshot_file(
    const std::filesystem::path& path
);

// Parses and returns validated section directory descriptors
[[nodiscard]] SnapshotResult parse_and_validate_directory(
    const std::uint8_t* data,
    std::size_t size,
    const WSnapHeader& header,
    std::vector<WSnapSectionDescriptor>& out_descriptors
) noexcept;

} // namespace winios::snapshot
