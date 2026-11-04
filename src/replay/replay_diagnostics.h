#pragma once

#include "replay/replay_scheduler.h"
#include "replay/replay_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayArtifactCheck / Report: Structural validation and association evidence
// -----------------------------------------------------------------------------
struct ReplayArtifactCheck {
    std::string name;
    bool passed{false};
    std::string detail;
};

struct ReplayArtifactReport {
    bool valid{false};
    std::string message;
    std::uint64_t wsnap_file_size{0};
    std::uint64_t sndl_file_size{0};
    std::uint64_t total_instructions{0};
    std::uint64_t total_records{0};
    std::uint64_t base_snapshot_hash{0};
    std::uint64_t actual_snapshot_hash{0};
    bool association_checked{false};
    bool association_match{false};
    std::vector<ReplayArtifactCheck> checks;
};

// -----------------------------------------------------------------------------
// ReplayVerification: Result of an exact-consumption replay verification
// -----------------------------------------------------------------------------
struct ReplayVerification {
    bool valid{false};
    std::string message;
    ReplayCompletionStatus completion_status{ReplayCompletionStatus::CompletedNormally};
    ReplayErrorCode error{ReplayErrorCode::Success};
    std::uint64_t instructions_executed{0};
    std::uint64_t events_consumed{0};
    std::uint64_t events_expected{0};
    std::uint64_t checkpoints_taken{0};
    bool diverged{false};
    ReplayComponentHashes hashes{};
};

// -----------------------------------------------------------------------------
// SMP Readiness (Stage 21 remains NOT READY; no multi-core replay is enabled)
// -----------------------------------------------------------------------------
struct SmpReadinessBlocker {
    std::string id;
    std::string area;
    std::string status;
    std::string detail;
};

struct SmpReadinessAssessment {
    bool ready{false};
    std::string verdict{"NOT READY"};
    std::string summary;
    std::vector<SmpReadinessBlocker> blockers;
    std::vector<std::string> prerequisites;
};

class ReplayDiagnostics {
public:
    [[nodiscard]] static ReplayArtifactReport validate_artifacts(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        bool require_association = true
    );

    [[nodiscard]] static ReplayVerification verify_replay(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        const ReplayConfig& config,
        bool require_exact_consumption = true
    );

    [[nodiscard]] static std::string format_artifact_report(
        const ReplayArtifactReport& report
    );

    [[nodiscard]] static std::string format_verification(
        const ReplayVerification& verification
    );

    [[nodiscard]] static SmpReadinessAssessment assess_smp_readiness();
    [[nodiscard]] static std::string format_smp_readiness(
        const SmpReadinessAssessment& assessment
    );
};

} // namespace winios::replay
