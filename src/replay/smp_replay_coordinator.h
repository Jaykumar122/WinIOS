#pragma once

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "device/dma_payload_store.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "replay/replay_types.h"
#include "replay/smp_replay_core_state.h"
#include "replay/smp_replay_cursor.h"
#include "replay/smp_replay_types.h"
#include "snapshot/smp_snapshot_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// SmpReplayCoordinator: Multi-Core Replay Coordinator & Event Synchronizer
// -----------------------------------------------------------------------------
class SmpReplayCoordinator {
public:
    explicit SmpReplayCoordinator(SmpReplayCapabilityConfig config = {});
    ~SmpReplayCoordinator();

    SmpReplayCoordinator(const SmpReplayCoordinator&) = delete;
    SmpReplayCoordinator& operator=(const SmpReplayCoordinator&) = delete;

    // Prepares replay session: validates snapshot and recording, executes capability gate,
    // restores snapshot baseline, and synchronizes initial state.
    [[nodiscard]] ReplayResult prepare(
        const std::filesystem::path& wsnap_path,
        const std::filesystem::path& sndl_path
    );

    // Runs multicore replay until completion, divergence, or instruction limit
    [[nodiscard]] ReplayResult run(std::uint64_t max_total_instructions = UINT64_MAX);

    // Executes a single scheduler quantum slice under deterministic replay control
    [[nodiscard]] bool step_quantum();

    // Checkpoint creation and restoration
    [[nodiscard]] SmpReplayCheckpoint create_checkpoint() const;
    [[nodiscard]] bool restore_checkpoint(const SmpReplayCheckpoint& checkpoint);

    // Cross-file identity & payload pre-validation
    [[nodiscard]] bool validate_cross_file_integrity(const std::filesystem::path& sndl_path, std::string& out_error);

    // Closes session and frees resources
    void close() noexcept;

    // Accessors
    [[nodiscard]] SmpReplayState state() const noexcept { return state_; }
    [[nodiscard]] const SmpReplayStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const SmpReplayDivergence& divergence() const noexcept { return divergence_; }
    [[nodiscard]] const SmpReplayCapabilityConfig& capability_config() const noexcept { return config_; }
    [[nodiscard]] std::size_t active_core_count() const noexcept { return active_core_count_; }
    [[nodiscard]] const SmpReplayEventCursor& cursor() const noexcept { return cursor_; }
    [[nodiscard]] SmpReplayEventCursor& cursor() noexcept { return cursor_; }
    [[nodiscard]] SmpScheduler* scheduler() noexcept { return scheduler_.get(); }
    [[nodiscard]] const SmpScheduler* scheduler() const noexcept { return scheduler_.get(); }
    [[nodiscard]] const device::DmaPayloadStore& dma_payload_store() const noexcept { return dma_payload_store_; }
    [[nodiscard]] device::DmaPayloadStore& dma_payload_store() noexcept { return dma_payload_store_; }
    [[nodiscard]] device::VirtualDeviceBus* device_bus() noexcept { return device_bus_.get(); }
    [[nodiscard]] const device::VirtualDeviceBus* device_bus() const noexcept { return device_bus_.get(); }
    [[nodiscard]] device::VirtualDmaDevice* dma_device() noexcept { return dma_device_.get(); }
    [[nodiscard]] const device::VirtualDmaDevice* dma_device() const noexcept { return dma_device_.get(); }

private:
    void trigger_divergence(SmpReplayDivergence div);
    [[nodiscard]] bool process_pending_external_events();
    [[nodiscard]] bool execute_core_quantum(std::size_t core_id, std::uint64_t quantum_budget);
    [[nodiscard]] bool verify_completion_invariants();

    SmpReplayCapabilityConfig config_;
    SmpReplayState state_{SmpReplayState::Uninitialized};
    std::filesystem::path wsnap_path_;
    std::filesystem::path sndl_path_;

    std::size_t active_core_count_{2};
    std::shared_ptr<SmpSharedMemory> shared_memory_;
    std::unique_ptr<SmpScheduler> scheduler_;
    std::shared_ptr<device::VirtualDeviceBus> device_bus_;
    std::shared_ptr<device::VirtualDmaDevice> dma_device_;
    device::DmaPayloadStore dma_payload_store_;
    std::vector<std::shared_ptr<CPU>> cpus_;
    std::vector<SmpReplayCoreState> core_replay_states_;

    SmpReplayEventCursor cursor_;
    SmpReplayDivergence divergence_;
    SmpReplayStats stats_;

    std::uint64_t baseline_snapshot_hash_{0};
};

} // namespace winios::replay
