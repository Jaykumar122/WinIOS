#pragma once

#include "replay/replay_profiler.h"
#include "replay/replay_state_hash.h"
#include <cstdint>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayCheckpoint: Architectural State Checkpoint at Instruction Boundaries
// -----------------------------------------------------------------------------
struct ReplayCheckpoint {
    std::uint64_t instruction_retirement_count{0};
    std::uint64_t pc{0};
    std::uint64_t sequence_num{0};
    ReplayComponentHashes hashes{};
};

// -----------------------------------------------------------------------------
// ReplayCheckpointManager: Manages Periodic & Boundary Verification Checkpoints
// -----------------------------------------------------------------------------
class ReplayCheckpointManager {
public:
    explicit ReplayCheckpointManager(std::uint64_t interval = 100000);

    void reset(std::uint64_t interval = 100000) noexcept;

    // Stage 21: Optional profile sink. Null disables all checkpoint timing/counters.
    void set_profiler(ReplayProfile* profile) noexcept { profiler_ = profile; }

    // Checks whether an instruction boundary matches the configured interval, and records a checkpoint
    bool maybe_record_checkpoint(
        const CPU& cpu,
        const Memory& memory,
        const VirtualTime* vt = nullptr,
        const GICv3* gic = nullptr,
        std::uint64_t current_seq = 0
    );

    // Forces recording a checkpoint (e.g. at initial baseline or final completion)
    void force_checkpoint(
        const CPU& cpu,
        const Memory& memory,
        const VirtualTime* vt = nullptr,
        const GICv3* gic = nullptr,
        std::uint64_t current_seq = 0
    );

    [[nodiscard]] const std::vector<ReplayCheckpoint>& checkpoints() const noexcept {
        return checkpoints_;
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return checkpoints_.size();
    }

    [[nodiscard]] std::uint64_t interval() const noexcept {
        return interval_;
    }

private:
    std::uint64_t interval_{100000};
    std::uint64_t last_checkpoint_ret_{0};
    std::vector<ReplayCheckpoint> checkpoints_;
    ReplayProfile* profiler_{nullptr};
};

} // namespace winios::replay
