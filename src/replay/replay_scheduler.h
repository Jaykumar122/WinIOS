#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_checkpoint.h"
#include "replay/replay_divergence.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_input_router.h"
#include "replay/replay_profiler.h"
#include "replay/replay_types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayExecutionStats: Metrics and Status for Completed or Interrupted Replay
// -----------------------------------------------------------------------------
struct ReplayExecutionStats {
    std::uint64_t instructions_executed{0};
    std::uint64_t events_consumed{0};
    std::uint64_t checkpoints_taken{0};
    double elapsed_ms{0.0};
    bool diverged{false};
    ReplayCompletionStatus completion_status{ReplayCompletionStatus::CompletedNormally};
    ReplayDivergenceInfo divergence_info{};
    ReplayComponentHashes final_state_hashes{};
};

// -----------------------------------------------------------------------------
// ReplayScheduler: Single-Core Deterministic Stepping Loop
// -----------------------------------------------------------------------------
class ReplayScheduler {
public:
    ReplayScheduler(CPU& cpu, Memory& memory, VirtualTime* vt, GICv3* gic,
                    ReplayEventCursor& cursor, ReplayInputRouter& router,
                    ReplayCheckpointManager& checkpoint_mgr,
                    const ReplayConfig& config,
                    ReplayProfile* profile = nullptr);

    ~ReplayScheduler() = default;

    ReplayScheduler(const ReplayScheduler&) = delete;
    ReplayScheduler& operator=(const ReplayScheduler&) = delete;

    // Executes the deterministic stepping loop until completion, divergence, or limit
    [[nodiscard]] ReplayResult run(ReplayExecutionStats& stats);

    // Executes one instruction step with pre-step and post-step checks
    [[nodiscard]] bool step_one(ReplayExecutionStats& stats);

    [[nodiscard]] std::uint64_t instructions_executed() const noexcept {
        return instructions_executed_;
    }

private:
    // Injects pending asynchronous inputs (interrupts, timer expirations) matching current retirement count
    bool inject_pending_asynchronous_inputs();

    CPU& cpu_;
    Memory& memory_;
    VirtualTime* vt_;
    GICv3* gic_;
    ReplayEventCursor& cursor_;
    ReplayInputRouter& router_;
    ReplayCheckpointManager& checkpoint_mgr_;
    ReplayConfig config_;
    ReplayProfile* profile_{nullptr};
    std::uint64_t instructions_executed_{0};
};

} // namespace winios::replay
