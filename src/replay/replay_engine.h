#pragma once

#include "replay/replay_profiler.h"
#include "replay/replay_session.h"
#include "replay/replay_types.h"

#include <filesystem>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayInspectionInfo: Summary of Snapshot, SNDL Log, and Association
// -----------------------------------------------------------------------------
struct ReplayInspectionInfo {
    bool valid{false};
    std::string wsnap_summary;
    std::string sndl_summary;
    std::uint64_t total_instructions{0};
    std::uint64_t total_records{0};
    std::uint64_t base_snapshot_hash{0};
    bool compatible{false};
    std::string diagnostics;
};

// -----------------------------------------------------------------------------
// ReplayEngine: Top-Level Public Facade for Deterministic Replay
// -----------------------------------------------------------------------------
class ReplayEngine {
public:
    // Non-mutating validation of snapshot, SNDL log, and their association
    [[nodiscard]] static ReplayResult validate(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path
    );

    // Runs a complete deterministic replay execution
    [[nodiscard]] static ReplayResult run(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        const ReplayConfig& config,
        ReplayExecutionStats& out_stats,
        ReplayProfile* out_profile = nullptr
    );

    // Inspects snapshot and SNDL log headers and structure without execution
    [[nodiscard]] static ReplayResult inspect(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        ReplayInspectionInfo& out_info
    );

    // Formats a human-readable summary of the inspection
    [[nodiscard]] static std::string format_inspection(
        const ReplayInspectionInfo& info
    );
};

} // namespace winios::replay
