#pragma once

#include "replay/replay_scheduler.h"
#include "replay/replay_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

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

// -----------------------------------------------------------------------------
// Stage 229.19 Replay Divergence Diagnostics
// -----------------------------------------------------------------------------
struct ReplayEventRecord {
    uint64_t    sequence{0};
    uint64_t    deterministic_tick{0};
    uint32_t    event_type{0};
    uint64_t    payload_hash{0};
    std::string description;
};

struct ReplayDivergence {
    bool        has_diverged{false};
    uint64_t    divergence_step{0};
    ReplayEventRecord expected;
    ReplayEventRecord actual;
    std::string reason;
};

class ReplayDivergenceChecker {
public:
    ReplayDivergenceChecker() = default;

    void add_recorded_event(ReplayEventRecord ev) {
        recorded_timeline_.push_back(std::move(ev));
    }

    void add_replayed_event(ReplayEventRecord ev) {
        replayed_timeline_.push_back(std::move(ev));
    }

    [[nodiscard]] ReplayDivergence check_divergence() const {
        ReplayDivergence div{};
        const size_t min_len = std::min(recorded_timeline_.size(), replayed_timeline_.size());

        for (size_t i = 0; i < min_len; ++i) {
            const auto& exp = recorded_timeline_[i];
            const auto& act = replayed_timeline_[i];

            if (exp.sequence != act.sequence) {
                div.has_diverged = true;
                div.divergence_step = i;
                div.expected = exp;
                div.actual = act;
                div.reason = "Sequence mismatch: expected " + std::to_string(exp.sequence) +
                             ", actual " + std::to_string(act.sequence);
                return div;
            }
            if (exp.deterministic_tick != act.deterministic_tick) {
                div.has_diverged = true;
                div.divergence_step = i;
                div.expected = exp;
                div.actual = act;
                div.reason = "Deterministic tick mismatch: expected " + std::to_string(exp.deterministic_tick) +
                             ", actual " + std::to_string(act.deterministic_tick);
                return div;
            }
            if (exp.event_type != act.event_type) {
                div.has_diverged = true;
                div.divergence_step = i;
                div.expected = exp;
                div.actual = act;
                div.reason = "Event type mismatch: expected " + std::to_string(exp.event_type) +
                             ", actual " + std::to_string(act.event_type);
                return div;
            }
            if (exp.payload_hash != act.payload_hash) {
                div.has_diverged = true;
                div.divergence_step = i;
                div.expected = exp;
                div.actual = act;
                div.reason = "Payload hash mismatch: expected 0x" + std::to_string(exp.payload_hash) +
                             ", actual 0x" + std::to_string(act.payload_hash);
                return div;
            }
        }

        if (recorded_timeline_.size() != replayed_timeline_.size()) {
            div.has_diverged = true;
            div.divergence_step = min_len;
            div.reason = "Timeline length mismatch: recorded=" + std::to_string(recorded_timeline_.size()) +
                         ", replayed=" + std::to_string(replayed_timeline_.size());
            return div;
        }

        div.has_diverged = false;
        div.reason = "Replay perfectly deterministic";
        return div;
    }

private:
    std::vector<ReplayEventRecord> recorded_timeline_;
    std::vector<ReplayEventRecord> replayed_timeline_;
};

} // namespace winios::replay

