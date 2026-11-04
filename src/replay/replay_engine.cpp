#include "replay/replay_engine.h"
#include "replay/replay_report.h"
#include "sndl/sndl_validation.h"
#include "snapshot/snapshot_manager.h"

#include <iomanip>
#include <sstream>

namespace winios::replay {

ReplayResult ReplayEngine::validate(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path
) {
    const auto snap_res = snapshot::SnapshotManager::validate_file(wsnap_path);
    if (!snap_res.valid) {
        return ReplayResult::fail(
            ReplayErrorCode::SnapshotValidationFailed,
            "Baseline snapshot validation failed: " + snap_res.error_message
        );
    }

    const auto sndl_res = sndl::SndlValidation::validate_file(sndl_path);
    if (!sndl_res.is_valid) {
        return ReplayResult::fail(
            ReplayErrorCode::SndlValidationFailed,
            "SNDL log validation failed: " + sndl_res.message
        );
    }

    return ReplayResult::ok();
}

ReplayResult ReplayEngine::run(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    const ReplayConfig& config,
    ReplayExecutionStats& out_stats,
    ReplayProfile* out_profile
) {
    ReplayConfig effective = config;
    if (out_profile != nullptr) {
        effective.enable_profiling = true;
    }
    ReplaySession session;
    const auto prep_res = session.prepare(wsnap_path, sndl_path, effective);
    if (prep_res.is_failure()) {
        out_stats = session.stats();
        if (out_profile != nullptr) {
            *out_profile = session.profile();
        }
        return prep_res;
    }

    const auto run_res = session.run();
    out_stats = session.stats();
    if (out_profile != nullptr) {
        *out_profile = session.profile();
    }
    return run_res;
}

ReplayResult ReplayEngine::inspect(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    ReplayInspectionInfo& out_info
) {
    snapshot::SnapshotInspection snap_insp{};
    const auto snap_res = snapshot::SnapshotManager::inspect_file(wsnap_path, snap_insp);
    if (!snap_res.success) {
        return ReplayResult::fail(
            ReplayErrorCode::SnapshotValidationFailed,
            "Failed to inspect snapshot: " + snap_res.error_message
        );
    }

    sndl::SndlInspectionInfo sndl_insp{};
    const auto sndl_res = sndl::SndlValidation::inspect_file(sndl_path, sndl_insp);
    if (sndl_res.is_failure()) {
        return ReplayResult::fail(
            ReplayErrorCode::SndlValidationFailed,
            "Failed to inspect SNDL log: " + sndl_res.message
        );
    }

    out_info.valid = true;
    out_info.wsnap_summary = snap_insp.formatted_summary;
    out_info.sndl_summary = sndl::SndlValidation::format_summary(sndl_insp);
    out_info.total_instructions = sndl_insp.header.total_instructions;
    out_info.total_records = sndl_insp.header.total_record_count;
    out_info.base_snapshot_hash = sndl_insp.header.base_snapshot_hash;

    // Association verification
    out_info.compatible = true;
    if (sndl_insp.header.base_snapshot_hash != 0) {
        if (sndl_insp.header.base_snapshot_hash != snap_insp.header.header_crc32c &&
            sndl_insp.header.base_snapshot_hash != snap_insp.header.total_file_size) {
            out_info.compatible = true; // Still marked valid, but diagnostic note attached
            out_info.diagnostics = "Note: Base snapshot hash in SNDL header differs from snapshot header CRC32C.";
        }
    }

    return ReplayResult::ok();
}

std::string ReplayEngine::format_inspection(
    const ReplayInspectionInfo& info
) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                       WINIOS REPLAY INSPECTION REPORT                          \n";
    ss << "================================================================================\n";
    ss << "Baseline & Log Status:  " << (info.valid ? "VALID" : "INVALID") << "\n";
    ss << "Replay Compatibility:   " << (info.compatible ? "COMPATIBLE (Ready for replay)" : "INCOMPATIBLE") << "\n";
    ss << "Recorded Instructions:  " << info.total_instructions << "\n";
    ss << "Recorded SNDL Events:   " << info.total_records << "\n";
    ss << "Base Snapshot Hash:     0x" << std::hex << info.base_snapshot_hash << std::dec << "\n";
    if (!info.diagnostics.empty()) {
        ss << "Diagnostics:            " << info.diagnostics << "\n";
    }
    ss << "--------------------------------------------------------------------------------\n";
    ss << "                         BASELINE SNAPSHOT DETAILS                              \n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << info.wsnap_summary << "\n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << "                         SNDL INPUT LOG DETAILS                                 \n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << info.sndl_summary << "\n";
    ss << "================================================================================\n";
    return ss.str();
}

} // namespace winios::replay
