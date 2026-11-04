#pragma once

#include "sndl/sndl_types.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace winios::replay {

// -----------------------------------------------------------------------------
// Replay Execution Mode
// -----------------------------------------------------------------------------
enum class ReplayMode : std::uint8_t {
    SingleCore = 1, // Supported deterministic execution
    MultiCore  = 2  // Unsupported / experimental
};

// -----------------------------------------------------------------------------
// Replay Lifecycle State
// -----------------------------------------------------------------------------
enum class ReplayLifecycleState : std::uint8_t {
    Idle              = 0,
    Preparing         = 1,
    Validating        = 2,
    RestoringBaseline = 3,
    Ready             = 4,
    Replaying         = 5,
    Diverged          = 6,
    Failed            = 7,
    Completed         = 8
};

[[nodiscard]] constexpr std::string_view replay_lifecycle_state_name(ReplayLifecycleState state) noexcept {
    switch (state) {
    case ReplayLifecycleState::Idle:              return "Idle";
    case ReplayLifecycleState::Preparing:         return "Preparing";
    case ReplayLifecycleState::Validating:        return "Validating";
    case ReplayLifecycleState::RestoringBaseline: return "RestoringBaseline";
    case ReplayLifecycleState::Ready:             return "Ready";
    case ReplayLifecycleState::Replaying:         return "Replaying";
    case ReplayLifecycleState::Diverged:          return "Diverged";
    case ReplayLifecycleState::Failed:            return "Failed";
    case ReplayLifecycleState::Completed:         return "Completed";
    default:                                      return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// Replay Completion Status (Stage 20 Determinism Hardening)
// -----------------------------------------------------------------------------
enum class ReplayCompletionStatus : std::uint32_t {
    CompletedNormally                     = 0, // All log events consumed, reached recorded retirement limit
    CompletedAtGuestHalt                  = 1, // CPU reached WFI/WFE or halted with all events consumed
    CompletedAtConfiguredInstructionLimit = 2, // Reached user-specified instruction limit without divergence
    IncompleteInputLog                    = 3, // CPU expected input but log was exhausted prematurely
    UnconsumedEvents                      = 4, // Execution stopped with unconsumed events remaining in log
    Diverged                              = 5, // Execution diverged from recorded log
    Failed                                = 6, // Hardware/interpreter/resource failure
    TimedOut                              = 7, // Execution exceeded configured timeout
    UnsupportedConfiguration              = 8  // E.g. SMP replay requested without override
};

[[nodiscard]] constexpr std::string_view replay_completion_status_name(ReplayCompletionStatus status) noexcept {
    switch (status) {
    case ReplayCompletionStatus::CompletedNormally:                     return "CompletedNormally";
    case ReplayCompletionStatus::CompletedAtGuestHalt:                  return "CompletedAtGuestHalt";
    case ReplayCompletionStatus::CompletedAtConfiguredInstructionLimit: return "CompletedAtConfiguredInstructionLimit";
    case ReplayCompletionStatus::IncompleteInputLog:                    return "IncompleteInputLog";
    case ReplayCompletionStatus::UnconsumedEvents:                      return "UnconsumedEvents";
    case ReplayCompletionStatus::Diverged:                              return "Diverged";
    case ReplayCompletionStatus::Failed:                                return "Failed";
    case ReplayCompletionStatus::TimedOut:                              return "TimedOut";
    case ReplayCompletionStatus::UnsupportedConfiguration:              return "UnsupportedConfiguration";
    default:                                                            return "UnknownCompletionStatus";
    }
}

// -----------------------------------------------------------------------------
// Divergence Category
// -----------------------------------------------------------------------------
enum class ReplayDivergenceCategory : std::uint32_t {
    None                 = 0,
    SnapshotMismatch     = 1,  // Baseline snapshot does not match
    EventSequenceMismatch= 2,  // Unexpected or missing sequence
    BoundaryMismatch     = 3,  // Current execution boundary differs
    RetirementMismatch   = 4,  // Instruction retirement count differs
    CoreMismatch         = 5,  // Event belongs to a different core
    EventTypeMismatch    = 6,  // Unexpected event type
    MMIOAddressMismatch  = 7,  // Address differs
    MMIOWidthMismatch    = 8,  // Access width differs
    InputValueMismatch   = 9,  // Consumed value differs
    InterruptMismatch    = 10, // Interrupt state or target differs
    TimerMismatch        = 11, // Timer state differs
    StateHashMismatch    = 12, // Architectural state hash differs
    MemoryMismatch       = 13, // Guest memory differs
    UnsupportedEvent     = 14, // Event cannot be replayed
    ReplayTimeout        = 15, // Execution exceeded configured limits
    InternalReplayError  = 16  // Replay subsystem failure
};

[[nodiscard]] constexpr std::string_view replay_divergence_category_name(ReplayDivergenceCategory cat) noexcept {
    switch (cat) {
    case ReplayDivergenceCategory::None:                  return "None";
    case ReplayDivergenceCategory::SnapshotMismatch:      return "SnapshotMismatch";
    case ReplayDivergenceCategory::EventSequenceMismatch: return "EventSequenceMismatch";
    case ReplayDivergenceCategory::BoundaryMismatch:      return "BoundaryMismatch";
    case ReplayDivergenceCategory::RetirementMismatch:    return "RetirementMismatch";
    case ReplayDivergenceCategory::CoreMismatch:          return "CoreMismatch";
    case ReplayDivergenceCategory::EventTypeMismatch:     return "EventTypeMismatch";
    case ReplayDivergenceCategory::MMIOAddressMismatch:   return "MMIOAddressMismatch";
    case ReplayDivergenceCategory::MMIOWidthMismatch:     return "MMIOWidthMismatch";
    case ReplayDivergenceCategory::InputValueMismatch:    return "InputValueMismatch";
    case ReplayDivergenceCategory::InterruptMismatch:     return "InterruptMismatch";
    case ReplayDivergenceCategory::TimerMismatch:         return "TimerMismatch";
    case ReplayDivergenceCategory::StateHashMismatch:     return "StateHashMismatch";
    case ReplayDivergenceCategory::MemoryMismatch:        return "MemoryMismatch";
    case ReplayDivergenceCategory::UnsupportedEvent:      return "UnsupportedEvent";
    case ReplayDivergenceCategory::ReplayTimeout:         return "ReplayTimeout";
    case ReplayDivergenceCategory::InternalReplayError:   return "InternalReplayError";
    default:                                              return "UnknownDivergence";
    }
}

// -----------------------------------------------------------------------------
// Replay Error Codes
// -----------------------------------------------------------------------------
enum class ReplayErrorCode : std::uint32_t {
    Success                 = 0,
    InvalidConfiguration    = 1,
    SnapshotValidationFailed= 2,
    SndlValidationFailed    = 3,
    SnapshotMismatch        = 4,
    SnapshotRestoreFailed   = 5,
    EventCursorError        = 6,
    Diverged                = 7,
    ReplayAborted           = 8,
    UnsupportedConfiguration= 9,
    FileIOError             = 10,
    StateVerificationFailed = 11
};

[[nodiscard]] constexpr std::string_view replay_error_name(ReplayErrorCode err) noexcept {
    switch (err) {
    case ReplayErrorCode::Success:                  return "Success";
    case ReplayErrorCode::InvalidConfiguration:     return "Invalid replay configuration";
    case ReplayErrorCode::SnapshotValidationFailed: return "Snapshot validation failed";
    case ReplayErrorCode::SndlValidationFailed:     return "SNDL validation failed";
    case ReplayErrorCode::SnapshotMismatch:         return "Snapshot hash/ID mismatch with recording";
    case ReplayErrorCode::SnapshotRestoreFailed:    return "Snapshot state restoration failed";
    case ReplayErrorCode::EventCursorError:         return "Event cursor stream error";
    case ReplayErrorCode::Diverged:                 return "Execution diverged from recording";
    case ReplayErrorCode::ReplayAborted:            return "Replay aborted";
    case ReplayErrorCode::UnsupportedConfiguration: return "Unsupported configuration (e.g. multi-core)";
    case ReplayErrorCode::FileIOError:              return "File I/O error";
    case ReplayErrorCode::StateVerificationFailed:  return "State verification failed";
    default:                                        return "Unknown replay error";
    }
}

// -----------------------------------------------------------------------------
// Replay Configuration
// -----------------------------------------------------------------------------
struct ReplayConfig {
    ReplayMode mode{ReplayMode::SingleCore};
    std::uint16_t target_core_id{0};
    std::uint64_t max_instructions{0};           // 0 = replay entire log
    std::uint64_t max_replay_duration_ms{0};     // 0 = unlimited
    std::uint64_t checkpoint_interval{100000};   // Instructions between checkpoints (0 = disabled)
    bool enable_wtr_tracing{false};              // Capture .wtr diagnostic trace during replay
    std::filesystem::path wtr_output_path{};
    std::filesystem::path divergence_report_path{"divergence_report.json"};
    bool verify_state_hashes{true};
    bool allow_experimental_smp{false};          // Must be explicitly true to run SMP
    bool allow_four_core_replay{false};          // Stage 27: Explicit opt-in for experimental 4-core SMP replay
    bool strict_bypass_checks{true};             // Stage 20: Prevent any unrecorded live input leakage
    bool require_exact_event_consumption{false}; // Stage 20: Require exact consumption of all SNDL records
    bool require_snapshot_association{true};     // Stage 21: Reject non-zero SNDL base hashes that do not match the snapshot
    bool enable_profiling{false};                // Stage 21: Opt-in phase profiling (never changes architectural state)
    std::filesystem::path profile_report_path{}; // Optional profile JSON path; implies enable_profiling
};

// -----------------------------------------------------------------------------
// Replay Result
// -----------------------------------------------------------------------------
struct ReplayResult {
    ReplayErrorCode code{ReplayErrorCode::Success};
    std::string message;

    [[nodiscard]] bool is_success() const noexcept { return code == ReplayErrorCode::Success; }
    [[nodiscard]] bool is_failure() const noexcept { return code != ReplayErrorCode::Success; }

    static ReplayResult ok() noexcept { return {ReplayErrorCode::Success, {}}; }
    static ReplayResult fail(ReplayErrorCode c, std::string msg = {}) {
        return {c, msg.empty() ? std::string(replay_error_name(c)) : std::move(msg)};
    }
};

} // namespace winios::replay
