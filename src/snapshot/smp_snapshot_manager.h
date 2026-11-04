#pragma once

#include "cpu/smp_scheduler.h"
#include "snapshot/smp_snapshot_types.h"
#include "snapshot/snapshot_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winios::snapshot {

class SmpSnapshotManager {
public:
    SmpSnapshotManager() = default;
    ~SmpSnapshotManager() = default;

    // Captures the complete multicore cluster state into an in-memory buffer (.wsnap v2)
    [[nodiscard]] static SnapshotResult capture_to_buffer(
        const SmpScheduler& scheduler,
        std::vector<std::uint8_t>& out_buffer,
        const SmpSnapshotCaptureOptions& options = {}
    );

    // Atomically captures and writes cluster snapshot to file
    [[nodiscard]] static SnapshotResult capture_to_file(
        const SmpScheduler& scheduler,
        const std::filesystem::path& path,
        const SmpSnapshotCaptureOptions& options = {}
    );

    // Validates a cluster snapshot buffer in memory
    [[nodiscard]] static SnapshotValidationResult validate_buffer(
        const std::uint8_t* data,
        std::size_t size
    ) noexcept;

    // Validates a cluster snapshot file on disk
    [[nodiscard]] static SnapshotValidationResult validate_file(
        const std::filesystem::path& path
    );

    // Atomically restores a multicore cluster from buffer
    [[nodiscard]] static SnapshotResult restore_from_buffer(
        const std::uint8_t* data,
        std::size_t size,
        SmpScheduler& scheduler
    );

    // Atomically restores a multicore cluster from file
    [[nodiscard]] static SnapshotResult restore_from_file(
        const std::filesystem::path& path,
        SmpScheduler& scheduler
    );

    // Inspects a cluster snapshot buffer and extracts detailed topology and state summary
    [[nodiscard]] static SnapshotResult inspect_buffer(
        const std::uint8_t* data,
        std::size_t size,
        SmpSnapshotInspection& out_inspection
    );

    // Inspects a cluster snapshot file on disk
    [[nodiscard]] static SnapshotResult inspect_file(
        const std::filesystem::path& path,
        SmpSnapshotInspection& out_inspection
    );

    // Compares two cluster snapshot files and generates a structural diff report
    [[nodiscard]] static SnapshotResult compare_snapshots(
        const std::filesystem::path& path1,
        const std::filesystem::path& path2,
        std::string& out_diff_report
    );
};

} // namespace winios::snapshot
