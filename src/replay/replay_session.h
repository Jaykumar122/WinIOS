#pragma once

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_checkpoint.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_input_router.h"
#include "replay/replay_profiler.h"
#include "replay/replay_scheduler.h"
#include "replay/replay_types.h"
#include "trace/trace_manager.h"

#include <filesystem>
#include <memory>
#include <string>

namespace winios::replay {

class SmpReplayCoordinator;

// -----------------------------------------------------------------------------
// ReplaySession: Full Lifecycle Coordinator for Deterministic Replay
// -----------------------------------------------------------------------------
class ReplaySession {
public:
    ReplaySession();
    ~ReplaySession();

    ReplaySession(const ReplaySession&) = delete;
    ReplaySession& operator=(const ReplaySession&) = delete;

    // Prepares session: validates baseline .wsnap and log .sndl, checks association,
    // and restores architectural baseline into isolated CPU/Memory/VirtualTime/GIC
    [[nodiscard]] ReplayResult prepare(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path,
        const ReplayConfig& config = {}
    );

    // Executes deterministic replay single-core stepping loop
    [[nodiscard]] ReplayResult run();

    // Executes a single instruction step
    [[nodiscard]] bool step();

    // Closes session resources and file handles
    void close() noexcept { cleanup(); }

    // Accessors
    [[nodiscard]] ReplayLifecycleState state() const noexcept { return state_; }
    [[nodiscard]] const ReplayConfig& config() const noexcept { return config_; }
    [[nodiscard]] const ReplayExecutionStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const ReplayProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] const ReplayCheckpointManager& checkpoint_manager() const noexcept { return checkpoint_mgr_; }
    [[nodiscard]] const ReplayEventCursor& cursor() const noexcept { return cursor_; }

    [[nodiscard]] CPU* cpu() noexcept { return cpu_.get(); }
    [[nodiscard]] const CPU* cpu() const noexcept { return cpu_.get(); }
    [[nodiscard]] Memory* memory() noexcept { return memory_.get(); }
    [[nodiscard]] const Memory* memory() const noexcept { return memory_.get(); }
    [[nodiscard]] VirtualTime* virtual_time() noexcept { return vt_.get(); }
    [[nodiscard]] const VirtualTime* virtual_time() const noexcept { return vt_.get(); }
    [[nodiscard]] GICv3* gic() noexcept { return gic_.get(); }
    [[nodiscard]] const GICv3* gic() const noexcept { return gic_.get(); }

    [[nodiscard]] SmpReplayCoordinator* smp_coordinator() noexcept { return smp_coordinator_.get(); }
    [[nodiscard]] const SmpReplayCoordinator* smp_coordinator() const noexcept { return smp_coordinator_.get(); }

private:
    void cleanup() noexcept;

    ReplayLifecycleState state_{ReplayLifecycleState::Idle};
    ReplayConfig config_{};
    std::filesystem::path wsnap_path_;
    std::filesystem::path sndl_path_;

    std::unique_ptr<CPU> cpu_;
    std::unique_ptr<Memory> memory_;
    std::unique_ptr<VirtualTime> vt_;
    std::unique_ptr<GICv3> gic_;
    std::unique_ptr<trace::TraceManager> trace_mgr_;

    ReplayEventCursor cursor_;
    std::unique_ptr<ReplayInputRouter> router_;
    ReplayCheckpointManager checkpoint_mgr_;
    std::unique_ptr<ReplayScheduler> scheduler_;
    std::unique_ptr<SmpReplayCoordinator> smp_coordinator_;
    ReplayExecutionStats stats_{};
    ReplayProfile profile_{};
};

} // namespace winios::replay
