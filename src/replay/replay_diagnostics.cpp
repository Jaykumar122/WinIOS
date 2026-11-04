#include "replay/replay_diagnostics.h"

#include "replay/replay_engine.h"
#include "replay/replay_report.h"
#include "replay/replay_session.h"
#include "sndl/sndl_validation.h"
#include "snapshot/snapshot_manager.h"

#include <iomanip>
#include <sstream>
#include <system_error>

namespace winios::replay {

namespace {

std::string hex_u64(std::uint64_t value) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

void add_check(
    ReplayArtifactReport& report,
    std::string name,
    bool passed,
    std::string detail
) {
    report.checks.push_back(ReplayArtifactCheck{
        std::move(name), passed, std::move(detail)});
    if (!passed) {
        report.valid = false;
    }
}

} // namespace

ReplayArtifactReport ReplayDiagnostics::validate_artifacts(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    bool require_association
) {
    ReplayArtifactReport report{};
    report.valid = true;

    std::error_code ec;
    const auto snap_size = std::filesystem::file_size(wsnap_path, ec);
    if (!ec) {
        report.wsnap_file_size = static_cast<std::uint64_t>(snap_size);
    }
    ec.clear();
    const auto sndl_size = std::filesystem::file_size(sndl_path, ec);
    if (!ec) {
        report.sndl_file_size = static_cast<std::uint64_t>(sndl_size);
    }

    const auto snap_val = snapshot::SnapshotManager::validate_file(wsnap_path);
    add_check(report, "wsnap_structure", snap_val.valid,
              snap_val.valid ? "WSNap header, directory, sections and CRCs valid"
                             : snap_val.error_message);

    const auto sndl_val = sndl::SndlValidation::validate_file(sndl_path);
    add_check(report, "sndl_structure", sndl_val.is_valid,
              sndl_val.is_valid ? sndl_val.message : sndl_val.message);
    report.total_instructions = sndl_val.total_instructions;
    report.total_records = sndl_val.total_records;
    report.base_snapshot_hash = sndl_val.base_snapshot_hash;

    snapshot::SnapshotInspection snap_info{};
    const auto inspect_res = snapshot::SnapshotManager::inspect_file(
        wsnap_path, snap_info);
    if (!inspect_res.success) {
        add_check(report, "wsnap_inspect", false, inspect_res.error_message);
    } else {
        add_check(report, "wsnap_inspect", true, snap_info.formatted_summary);
        report.actual_snapshot_hash = snap_info.header.header_crc32c;
    }

    if (require_association && sndl_val.base_snapshot_hash != 0 && inspect_res.success) {
        report.association_checked = true;
        report.association_match =
            (sndl_val.base_snapshot_hash == snap_info.header.header_crc32c) ||
            (sndl_val.base_snapshot_hash ==
             static_cast<std::uint64_t>(snap_info.header.total_file_size));
        add_check(report, "snapshot_association", report.association_match,
                  report.association_match
                      ? "SNDL base snapshot hash matches .wsnap CRC32C or file size"
                      : "SNDL base snapshot hash does not match .wsnap CRC32C or file size");
    } else {
        add_check(report, "snapshot_association", true,
                  sndl_val.base_snapshot_hash == 0
                      ? "SNDL did not record a base snapshot hash (association not enforced)"
                      : "association check skipped");
    }

    if (!report.valid) {
        report.message = "artifact validation failed";
    } else {
        report.message = "artifact validation succeeded";
    }
    return report;
}

ReplayVerification ReplayDiagnostics::verify_replay(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    const ReplayConfig& config,
    bool require_exact_consumption
) {
    ReplayVerification verification{};
    ReplayConfig effective = config;
    effective.require_exact_event_consumption = require_exact_consumption;
    effective.profile_report_path.clear();
    effective.enable_profiling = false;

    ReplaySession session;
    const auto prep = session.prepare(wsnap_path, sndl_path, effective);
    if (prep.is_failure()) {
        verification.error = prep.code;
        verification.message = prep.message;
        verification.completion_status = session.stats().completion_status;
        return verification;
    }

    verification.events_expected = session.cursor().header().total_record_count;
    const auto run = session.run();
    const auto& stats = session.stats();

    verification.error = run.code;
    verification.message = run.is_success() ? "replay completed" : run.message;
    verification.completion_status = stats.completion_status;
    verification.instructions_executed = stats.instructions_executed;
    verification.events_consumed = stats.events_consumed;
    verification.checkpoints_taken = stats.checkpoints_taken;
    verification.diverged = stats.diverged;
    verification.hashes = stats.final_state_hashes;
    verification.valid = run.is_success() &&
                         !stats.diverged &&
                         (!require_exact_consumption ||
                          (stats.events_consumed == verification.events_expected));
    if (verification.valid && require_exact_consumption) {
        verification.message = "replay completed with exact SNDL event consumption";
    }
    return verification;
}

std::string ReplayDiagnostics::format_artifact_report(const ReplayArtifactReport& r) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                WINIOS STAGE 21 REPLAY ARTIFACT DIAGNOSTICS                     \n";
    ss << "================================================================================\n";
    ss << "Result:                 " << (r.valid ? "VALID" : "INVALID") << "\n";
    ss << "Message:                " << r.message << "\n";
    ss << "WSNap Size:             " << r.wsnap_file_size << " bytes\n";
    ss << "SNDL Size:              " << r.sndl_file_size << " bytes\n";
    ss << "Recorded Instructions:  " << r.total_instructions << "\n";
    ss << "Recorded Records:       " << r.total_records << "\n";
    ss << "SNDL Base Hash:         " << hex_u64(r.base_snapshot_hash) << "\n";
    ss << "WSnap Header Hash:      " << hex_u64(r.actual_snapshot_hash) << "\n";
    ss << "Association Checked:    " << (r.association_checked ? "yes" : "no") << "\n";
    ss << "Association Match:      " << (r.association_match ? "yes" : "no") << "\n";
    ss << "--------------------------------------------------------------------------------\n";
    for (const auto& check : r.checks) {
        ss << "[" << (check.passed ? "PASS" : "FAIL") << "] " << check.name << "\n";
        ss << "       " << check.detail << "\n";
    }
    ss << "================================================================================\n";
    return ss.str();
}

std::string ReplayDiagnostics::format_verification(const ReplayVerification& v) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                WINIOS STAGE 21 REPLAY VERIFICATION                             \n";
    ss << "================================================================================\n";
    ss << "Result:                 " << (v.valid ? "VERIFIED" : "FAILED") << "\n";
    ss << "Message:                " << v.message << "\n";
    ss << "Completion Status:      " << replay_completion_status_name(v.completion_status) << "\n";
    ss << "Diverged:               " << (v.diverged ? "yes" : "no") << "\n";
    ss << "Instructions Executed:  " << v.instructions_executed << "\n";
    ss << "Events Consumed:        " << v.events_consumed << " / " << v.events_expected << "\n";
    ss << "Checkpoints Taken:      " << v.checkpoints_taken << "\n";
    ss << "CPU Registers Hash:     " << hex_u64(v.hashes.cpu_registers_hash) << "\n";
    ss << "SIMD Registers Hash:    " << hex_u64(v.hashes.simd_registers_hash) << "\n";
    ss << "System Registers Hash:  " << hex_u64(v.hashes.system_registers_hash) << "\n";
    ss << "Guest Memory Hash:      " << hex_u64(v.hashes.memory_hash) << "\n";
    ss << "Timer Hash:             " << hex_u64(v.hashes.timer_hash) << "\n";
    ss << "GICv3 Hash:             " << hex_u64(v.hashes.gic_hash) << "\n";
    ss << "Combined Hash:          " << hex_u64(v.hashes.combined_hash) << "\n";
    ss << "================================================================================\n";
    return ss.str();
}

SmpReadinessAssessment ReplayDiagnostics::assess_smp_readiness() {
    SmpReadinessAssessment a{};
    a.ready = false;
    a.verdict = "NOT READY";
    a.summary =
        "Stage 21 keeps deterministic replay strictly single-core. The existing "
        "SMPCluster and experimental multi-core paths are not replay-capable: they "
        "lack inter-core ordering records, replayable memory visibility, "
        "multi-core checkpoints, and deterministic GIC/counter serialization.";

    a.blockers = {
        {"SMP-01", "SNDL event model",
         "OPEN",
         "SNDL carries only external-input events (MMIO, interrupt, timer, counter, "
         "entropy). There are no core start/stop, barrier, release/acquire, "
         "exclusive-monitor, or inter-core visibility records."},
        {"SMP-02", "Inter-core ordering",
         "OPEN",
         "No global replay order exists for stores observed by another core. Host "
         "thread scheduling and lock acquisition order are not recorded."},
        {"SMP-03", "Memory visibility",
         "OPEN",
         "Replay Memory is flat RAM with unlocked deterministic single-core access. "
         "The striped concurrent mode provides host mutual exclusion, not a "
         "recorded architectural visibility order."},
        {"SMP-04", "Shared virtual time",
         "OPEN",
         "VirtualTime uses a shared atomic physical counter incremented per stepped "
         "instruction. With multiple cores the counter value depends on interleaving "
         "and is not reproducible from the current SNDL stream."},
        {"SMP-05", "GICv3 cross-core delivery",
         "OPEN",
         "The GICv3 distributor is shared and uses a recursive mutex. Cross-core SGI "
         "and PPI assertion/acknowledgement order is not recorded or replayed."},
        {"SMP-06", "Checkpoint and hash coverage",
         "OPEN",
         "ReplayCheckpoint and ReplayStateHash cover one CPU, one memory image, one "
         "timer set, and per-core GIC views. There is no cluster-wide checkpoint or "
         "multi-core canonical hash."},
        {"SMP-07", "Session and cursor model",
         "OPEN",
         "ReplaySession, ReplayScheduler, ReplayInputRouter and ReplayEventCursor are "
         "single-core. ReplayConfig has no core count, per-core cursors, or "
         "multi-core completion semantics."},
        {"SMP-08", "Host scheduling determinism",
         "OPEN",
         "SMPCluster::run_deterministic provides round-robin guest stepping but the "
         "optional parallel engine uses host threads; neither records event order "
         "nor supports checkpoints/hashes for multi-core replay."},
        {"SMP-09", "Divergence localization",
         "OPEN",
         "Divergence reports identify one core and one cursor. There is no "
         "cross-core first-divergence ordering or per-core expected-hash matrix."},
        {"SMP-10", "Validation evidence",
         "OPEN",
         "No 2-core or 4-core large recording matrix, stress run, or cross-build "
         "repeatability evidence exists for multi-core replay."}
    };

    a.prerequisites = {
        "Extend SNDL with explicit inter-core synchronization, visibility, core "
        "lifecycle, and atomic/exclusive events including a total replay order.",
        "Define and implement a replayable memory consistency model with recorded "
        "store visibility and exclusive-monitor ownership.",
        "Make VirtualTime and GICv3 delivery order driven by recorded events rather "
        "than host scheduling.",
        "Add cluster-wide checkpoints, per-core canonical hashes, and multi-core "
        "divergence localization.",
        "Add a ReplayConfig core matrix, per-core cursors, multi-core scheduler, and "
        "strict UnsupportedConfiguration gating until all evidence passes.",
        "Produce large 2-core/4-core validation, stress, and repeatability evidence "
        "before changing the readiness verdict."
    };
    return a;
}

std::string ReplayDiagnostics::format_smp_readiness(
    const SmpReadinessAssessment& a
) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                WINIOS STAGE 21 SMP REPLAY READINESS                            \n";
    ss << "================================================================================\n";
    ss << "Verdict:                " << a.verdict << "\n";
    ss << "SMP Replay Enabled:     no\n";
    ss << "Summary:                " << a.summary << "\n";
    ss << "--------------------------------------------------------------------------------\n";
    for (const auto& b : a.blockers) {
        ss << "[" << b.status << "] " << b.id << " (" << b.area << ")\n";
        ss << "       " << b.detail << "\n";
    }
    ss << "--------------------------------------------------------------------------------\n";
    ss << "Prerequisites before readiness can change:\n";
    for (std::size_t i = 0; i < a.prerequisites.size(); ++i) {
        ss << "  " << (i + 1) << ". " << a.prerequisites[i] << "\n";
    }
    ss << "================================================================================\n";
    return ss.str();
}

} // namespace winios::replay
