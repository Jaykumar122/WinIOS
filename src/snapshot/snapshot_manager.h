#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_types.h"
#include "snapshot/wsnap_format.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winios::snapshot {

struct SnapshotCaptureOptions {
    std::string description{"WinIOS Snapshot"};
    std::string creator{"WinIOS SnapshotEngine"};
    bool include_memory{true};
    bool include_timers{true};
    bool include_gic{true};
    bool include_pmu{true};
    bool include_debug{true};
};

struct SnapshotInspection {
    WSnapHeader header{};
    std::vector<WSnapSectionDescriptor> sections{};
    std::string metadata_description;
    std::string metadata_creator;
    std::string formatted_summary;
};

class SnapshotManager {
public:
    SnapshotManager() = default;
    ~SnapshotManager() = default;

    // Captures complete system state into an in-memory .wsnap serialized buffer
    [[nodiscard]] static SnapshotResult capture_to_buffer(
        const SnapshotRestoreContext& context,
        std::vector<std::uint8_t>& out_buffer,
        const SnapshotCaptureOptions& options = {}
    );

    // Captures and writes .wsnap directly to file
    [[nodiscard]] static SnapshotResult capture_to_file(
        const SnapshotRestoreContext& context,
        const std::filesystem::path& path,
        const SnapshotCaptureOptions& options = {}
    );

    // Loads a .wsnap file into memory
    [[nodiscard]] static SnapshotResult load_from_file(
        const std::filesystem::path& path,
        std::vector<std::uint8_t>& out_buffer
    );

    // Restores an emulator instance directly from a .wsnap file
    [[nodiscard]] static SnapshotResult restore_from_file(
        const std::filesystem::path& path,
        SnapshotRestoreContext& context
    );

    // Validates a .wsnap file on disk
    [[nodiscard]] static SnapshotValidationResult validate_file(
        const std::filesystem::path& path
    );

    // Inspects a .wsnap file on disk and extracts human-readable summary
    [[nodiscard]] static SnapshotResult inspect_file(
        const std::filesystem::path& path,
        SnapshotInspection& out_inspection
    );
};

} // namespace winios::snapshot
