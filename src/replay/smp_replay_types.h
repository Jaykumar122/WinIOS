#pragma once

#include "cpu/smp_core_state.h"
#include "cpu/smp_scheduler.h"
#include "sndl/sndl_v2_types.h"
#include "trace/trace_serializer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// SmpReplayState: State Machine for Multicore Deterministic Replay
// -----------------------------------------------------------------------------
enum class SmpReplayState : std::uint32_t {
    Uninitialized    = 0,
    Preparing        = 1,
    SnapshotRestored = 2,
    Ready            = 3,
    Running          = 4,
    Paused           = 5,
    Completed        = 6,
    Diverged         = 7,
    Aborted          = 8,
    Unsupported      = 9
};

[[nodiscard]] constexpr std::string_view smp_replay_state_name(SmpReplayState s) noexcept {
    switch (s) {
    case SmpReplayState::Uninitialized:    return "Uninitialized";
    case SmpReplayState::Preparing:        return "Preparing";
    case SmpReplayState::SnapshotRestored: return "SnapshotRestored";
    case SmpReplayState::Ready:            return "Ready";
    case SmpReplayState::Running:          return "Running";
    case SmpReplayState::Paused:           return "Paused";
    case SmpReplayState::Completed:        return "Completed";
    case SmpReplayState::Diverged:         return "Diverged";
    case SmpReplayState::Aborted:          return "Aborted";
    case SmpReplayState::Unsupported:      return "Unsupported";
    default:                               return "Unknown";
    }
}

[[nodiscard]] constexpr bool is_valid_smp_replay_transition(SmpReplayState from, SmpReplayState to) noexcept {
    if (from == to) return true;
    switch (from) {
    case SmpReplayState::Uninitialized:
        return to == SmpReplayState::Preparing || to == SmpReplayState::Unsupported;
    case SmpReplayState::Preparing:
        return to == SmpReplayState::SnapshotRestored ||
               to == SmpReplayState::Aborted ||
               to == SmpReplayState::Unsupported ||
               to == SmpReplayState::Diverged;
    case SmpReplayState::SnapshotRestored:
        return to == SmpReplayState::Ready ||
               to == SmpReplayState::Aborted ||
               to == SmpReplayState::Diverged;
    case SmpReplayState::Ready:
        return to == SmpReplayState::Running ||
               to == SmpReplayState::Aborted;
    case SmpReplayState::Running:
        return to == SmpReplayState::Paused ||
               to == SmpReplayState::Completed ||
               to == SmpReplayState::Diverged ||
               to == SmpReplayState::Aborted;
    case SmpReplayState::Paused:
        return to == SmpReplayState::Running ||
               to == SmpReplayState::Aborted;
    case SmpReplayState::Completed:
    case SmpReplayState::Diverged:
    case SmpReplayState::Aborted:
    case SmpReplayState::Unsupported:
        return false; // Terminal states cannot transition
    default:
        return false;
    }
}

// -----------------------------------------------------------------------------
// SmpReplayDivergenceType: Diagnostic Classification of Replay Divergence
// -----------------------------------------------------------------------------
enum class SmpReplayDivergenceType : std::uint32_t {
    None                     = 0,
    UnexpectedEvent          = 1,
    MissingEvent             = 2,
    SequenceMismatch         = 3,
    CoreMismatch             = 4,
    SchedulerMismatch        = 5,
    QuantumMismatch          = 6,
    RetirementMismatch       = 7,
    InterruptMismatch        = 8,
    TimerMismatch            = 9,
    MmioMismatch             = 10,
    EntropyMismatch          = 11,
    LifecycleMismatch        = 12,
    SnapshotMismatch         = 13,
    FinalStateMismatch       = 14,
    UnsupportedExternalInput = 15,
    IncompleteRecording      = 16,
    DmaDataMismatch          = 17
};

[[nodiscard]] constexpr std::string_view smp_replay_divergence_type_name(SmpReplayDivergenceType t) noexcept {
    switch (t) {
    case SmpReplayDivergenceType::None:                     return "None";
    case SmpReplayDivergenceType::UnexpectedEvent:          return "UnexpectedEvent";
    case SmpReplayDivergenceType::MissingEvent:             return "MissingEvent";
    case SmpReplayDivergenceType::SequenceMismatch:         return "SequenceMismatch";
    case SmpReplayDivergenceType::CoreMismatch:             return "CoreMismatch";
    case SmpReplayDivergenceType::SchedulerMismatch:        return "SchedulerMismatch";
    case SmpReplayDivergenceType::QuantumMismatch:          return "QuantumMismatch";
    case SmpReplayDivergenceType::RetirementMismatch:       return "RetirementMismatch";
    case SmpReplayDivergenceType::InterruptMismatch:        return "InterruptMismatch";
    case SmpReplayDivergenceType::TimerMismatch:            return "TimerMismatch";
    case SmpReplayDivergenceType::MmioMismatch:             return "MmioMismatch";
    case SmpReplayDivergenceType::EntropyMismatch:          return "EntropyMismatch";
    case SmpReplayDivergenceType::LifecycleMismatch:        return "LifecycleMismatch";
    case SmpReplayDivergenceType::SnapshotMismatch:         return "SnapshotMismatch";
    case SmpReplayDivergenceType::FinalStateMismatch:       return "FinalStateMismatch";
    case SmpReplayDivergenceType::UnsupportedExternalInput: return "UnsupportedExternalInput";
    case SmpReplayDivergenceType::IncompleteRecording:      return "IncompleteRecording";
    case SmpReplayDivergenceType::DmaDataMismatch:          return "DmaDataMismatch";
    default:                                                return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// SmpReplayDivergence: Detailed Diagnostic Payload
// -----------------------------------------------------------------------------
struct SmpReplayDivergence {
    SmpReplayDivergenceType type{SmpReplayDivergenceType::None};
    std::string message;
    std::uint64_t global_event_sequence{0};
    std::uint64_t global_retirement_sequence{0};
    std::uint64_t core_retirement_count{0};
    std::uint32_t core_id{0};
    sndl::SndlV2EventType expected_event_type{sndl::SndlV2EventType::Unknown};
    sndl::SndlV2EventType actual_event_type{sndl::SndlV2EventType::Unknown};
    std::uint64_t expected_val{0};
    std::uint64_t actual_val{0};
    std::uint64_t quantum_budget{0};
    std::uint64_t quantum_executed{0};
    std::uint64_t pc{0};

    [[nodiscard]] bool has_diverged() const noexcept {
        return type != SmpReplayDivergenceType::None;
    }
};

// -----------------------------------------------------------------------------
// SmpReplayClockSource: Clock Source Classification for Deterministic Replay
// -----------------------------------------------------------------------------
enum class SmpReplayClockSource : std::uint32_t {
    DeterministicVirtualClock = 0,
    RecordedExternalClock     = 1,
    UnsupportedHostClock      = 2
};

[[nodiscard]] constexpr std::string_view smp_replay_clock_source_name(SmpReplayClockSource cs) noexcept {
    switch (cs) {
    case SmpReplayClockSource::DeterministicVirtualClock: return "DeterministicVirtualClock";
    case SmpReplayClockSource::RecordedExternalClock:     return "RecordedExternalClock";
    case SmpReplayClockSource::UnsupportedHostClock:      return "UnsupportedHostClock";
    default:                                              return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// SmpReplayCapabilityConfig: Capability Gate and Replay Policy
// -----------------------------------------------------------------------------
struct SmpReplayCapabilityConfig {
    std::size_t max_supported_cores{2};            // By default 2; set to 4 or 8 if enabled
    bool allow_experimental_smp{false};          // Required to be true for any multi-core replay
    bool allow_four_core_replay{false};            // Stage 27: Explicit 4-core opt-in capability
    bool allow_eight_core_replay{false};           // Stage 29: Explicit 8-core opt-in capability
    SmpReplayClockSource clock_source{SmpReplayClockSource::DeterministicVirtualClock}; // Stage 27: Clock capability
    bool allow_unvirtualized_wall_clock{false};    // BLK-SMP-04 rejection guard
    bool allow_uncaptured_dma{false};              // BLK-SMP-03 rejection guard
    bool allow_virtual_devices{false};             // Stage 31: Virtual device execution capability
    bool allow_virtual_dma{false};                 // Stage 31: Virtual DMA capability
    bool allow_dma_recording{false};               // Stage 31: Virtual DMA recording capability
    bool allow_dma_replay{false};                  // Stage 31: Virtual DMA deterministic replay capability
    bool require_completed_footer{true};          // Rejects incomplete/aborted/overflow recordings
    bool require_snapshot_association{true};       // Enforces canonical cluster hash parity
    bool enforce_strict_monotonicity{true};        // S_{i+1} = S_i + 1 sequence check
    bool enforce_quantum_matching{true};           // Verifies scheduler quantum slice decisions
    bool enforce_retirement_parity{true};          // Verifies exact instruction retirements
    bool verify_final_cluster_hash{true};          // Canonical cluster state comparison at end
};

// -----------------------------------------------------------------------------
// SmpReplayStats: Execution Summary & Metrics
// -----------------------------------------------------------------------------
struct SmpReplayStats {
    std::uint64_t total_events_consumed{0};
    std::uint64_t total_quantums_executed{0};
    std::uint64_t total_instructions_retired{0};
    std::vector<std::uint64_t> per_core_retired;
    std::uint64_t final_canonical_hash{0};
    std::uint64_t execution_microseconds{0};
    SmpReplayState final_state{SmpReplayState::Uninitialized};
    SmpReplayDivergence divergence;
};

// -----------------------------------------------------------------------------
// SmpReplayCheckpoint: Deterministic SMP Replay State Boundary
// -----------------------------------------------------------------------------
struct SmpReplayCheckpoint {
    static constexpr std::uint32_t kMagic = 0x534D5043u; // 'SMPC'
    static constexpr std::uint32_t kVersion = 1;

    std::uint32_t magic{kMagic};
    std::uint32_t version{kVersion};
    std::uint64_t session_id{0};
    std::uint64_t base_snapshot_hash{0};
    std::uint64_t global_event_sequence{0};
    std::uint64_t global_retirement_sequence{0};
    std::uint64_t checkpoint_cluster_hash{0};
    std::size_t active_core_count{0};
    std::vector<std::uint64_t> per_core_retired;
    std::vector<std::uint64_t> per_core_pc;
    std::vector<SmpCoreState> per_core_states;
    std::vector<std::uint8_t> memory_content;
    std::uint64_t dma_payload_store_offset{0};
    std::vector<std::uint8_t> dma_device_state;
    std::uint32_t crc32c{0};

    [[nodiscard]] std::uint32_t compute_crc32c() const noexcept {
        std::uint32_t crc = 0xFFFFFFFFu;
        auto hash_bytes = [&](const void* ptr, std::size_t len) {
            crc = trace::calculate_crc32c(ptr, len, crc);
        };
        hash_bytes(&magic, sizeof(magic));
        hash_bytes(&version, sizeof(version));
        hash_bytes(&session_id, sizeof(session_id));
        hash_bytes(&base_snapshot_hash, sizeof(base_snapshot_hash));
        hash_bytes(&global_event_sequence, sizeof(global_event_sequence));
        hash_bytes(&global_retirement_sequence, sizeof(global_retirement_sequence));
        hash_bytes(&checkpoint_cluster_hash, sizeof(checkpoint_cluster_hash));
        const std::uint64_t cc = active_core_count;
        hash_bytes(&cc, sizeof(cc));
        if (!per_core_retired.empty()) {
            hash_bytes(per_core_retired.data(), per_core_retired.size() * sizeof(std::uint64_t));
        }
        if (!per_core_pc.empty()) {
            hash_bytes(per_core_pc.data(), per_core_pc.size() * sizeof(std::uint64_t));
        }
        for (const auto& s : per_core_states) {
            const std::uint64_t h = s.compute_canonical_hash();
            hash_bytes(&h, sizeof(h));
        }
        if (!memory_content.empty()) {
            hash_bytes(memory_content.data(), memory_content.size());
        }
        hash_bytes(&dma_payload_store_offset, sizeof(dma_payload_store_offset));
        if (!dma_device_state.empty()) {
            hash_bytes(dma_device_state.data(), dma_device_state.size());
        }
        return crc ^ 0xFFFFFFFFu;
    }

    [[nodiscard]] bool verify_crc() const noexcept {
        return crc32c == compute_crc32c();
    }
};

} // namespace winios::replay
