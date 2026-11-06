#include "replay/replay_profiler.h"
#include "replay/replay_session.h"
#include "replay/replay_report.h"
#include "replay/smp_replay_coordinator.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_validation.h"
#include "snapshot/wsnap_format.h"

#include <iostream>
#include <system_error>

namespace winios::replay {

ReplaySession::ReplaySession() = default;

ReplaySession::~ReplaySession() {
    cleanup();
}

void ReplaySession::cleanup() noexcept {
    set_active_replay_router(nullptr);
    smp_coordinator_.reset();
    scheduler_.reset();
    router_.reset();
    trace_mgr_.reset();
    cursor_.close();
    cpu_.reset();
    memory_.reset();
    vt_.reset();
    gic_.reset();
    state_ = ReplayLifecycleState::Idle;
}

ReplayResult ReplaySession::prepare(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path,
    const ReplayConfig& config
) {
    cleanup();
    config_ = config;
    wsnap_path_ = wsnap_path;
    sndl_path_ = sndl_path;
    state_ = ReplayLifecycleState::Preparing;

    profile_ = {};
    profile_.enabled = config_.enable_profiling || !config_.profile_report_path.empty();
    profile_.label = wsnap_path.filename().string() + " + " + sndl_path.filename().string();
    config_.enable_profiling = profile_.enabled;
    ReplayProfile* prof = profile_.enabled ? &profile_ : nullptr;
    ReplayProfileScope prepare_scope(prof ? &prof->prepare_total : nullptr);

    {
        std::error_code ec;
        const auto snap_size = std::filesystem::file_size(wsnap_path, ec);
        if (!ec) {
            profile_.wsnap_file_size = static_cast<std::uint64_t>(snap_size);
        }
        ec.clear();
        const auto sndl_size = std::filesystem::file_size(sndl_path, ec);
        if (!ec) {
            profile_.sndl_file_size = static_cast<std::uint64_t>(sndl_size);
        }
    }

    // Historical guard: reject MultiCore mode unless explicitly enabled
    if (config_.mode == ReplayMode::MultiCore && !config_.allow_experimental_smp) {
        state_ = ReplayLifecycleState::Failed;
        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core SMP replay is unsupported/experimental without explicit capability gate enablement."
        );
    }

    // Multi-core capability gate: route multi-core snapshots to SmpReplayCoordinator
    {
        std::ifstream file_check(wsnap_path, std::ios::binary);
        if (file_check.is_open()) {
            std::uint8_t hdr_bytes[64];
            if (file_check.read(reinterpret_cast<char*>(hdr_bytes), 64)) {
                const std::uint16_t cc = trace::decode_u16_le(hdr_bytes + 10);
                if (cc > 1) {
                    if (cc == 8) {
                        if (!config_.allow_experimental_smp || !config_.allow_eight_core_replay) {
                            state_ = ReplayLifecycleState::Failed;
                            stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
                            return ReplayResult::fail(
                                ReplayErrorCode::UnsupportedConfiguration,
                                "Eight-core SMP replay is experimental and requires allow_eight_core_replay = true."
                            );
                        }
                    } else if (cc == 4) {
                        if (!config_.allow_four_core_replay) {
                            state_ = ReplayLifecycleState::Failed;
                            stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
                            return ReplayResult::fail(
                                ReplayErrorCode::UnsupportedConfiguration,
                                "Four-core SMP replay is experimental and requires allow_four_core_replay = true."
                            );
                        }
                    } else if (cc != 2) {
                        state_ = ReplayLifecycleState::Failed;
                        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
                        return ReplayResult::fail(
                            ReplayErrorCode::UnsupportedConfiguration,
                            "Multi-core SMP replay is unsupported: snapshot specifies " +
                            std::to_string(cc) + " cores. (Only 2-core, 4-core, and experimental 8-core replay are supported)."
                        );
                    }
                    if (!config_.allow_experimental_smp) {
                        state_ = ReplayLifecycleState::Failed;
                        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
                        return ReplayResult::fail(
                            ReplayErrorCode::UnsupportedConfiguration,
                            "Multi-core SMP replay is unsupported/experimental without explicit capability gate enablement."
                        );
                    }

                    SmpReplayCapabilityConfig smp_cfg{};
                    smp_cfg.max_supported_cores = cc;
                    smp_cfg.allow_experimental_smp = config_.allow_experimental_smp;
                    smp_cfg.allow_four_core_replay = config_.allow_four_core_replay;
                    smp_cfg.allow_eight_core_replay = config_.allow_eight_core_replay;
                    smp_coordinator_ = std::make_unique<SmpReplayCoordinator>(smp_cfg);
                    const auto smp_prep = smp_coordinator_->prepare(wsnap_path, sndl_path);
                    if (smp_prep.is_failure()) {
                        state_ = ReplayLifecycleState::Failed;
                        return smp_prep;
                    }
                    state_ = ReplayLifecycleState::Ready;
                    return ReplayResult::ok();
                }
            }
        }
    }

    state_ = ReplayLifecycleState::Validating;

    // Validate baseline snapshot on disk
    snapshot::SnapshotValidationResult snap_val{};
    {
        ReplayProfileScope scope(prof ? &prof->snapshot_validate : nullptr);
        snap_val = snapshot::SnapshotManager::validate_file(wsnap_path);
    }
    if (!snap_val.valid) {
        state_ = ReplayLifecycleState::Failed;
        return ReplayResult::fail(
            ReplayErrorCode::SnapshotValidationFailed,
            "Baseline snapshot validation failed: " + snap_val.error_message
        );
    }

    // Inspect snapshot to get memory capacity and verify single-core configuration
    snapshot::SnapshotInspection snap_info{};
    snapshot::SnapshotResult inspect_res{};
    {
        ReplayProfileScope scope(prof ? &prof->snapshot_inspect : nullptr);
        inspect_res = snapshot::SnapshotManager::inspect_file(wsnap_path, snap_info);
    }
    if (!inspect_res.success) {
        state_ = ReplayLifecycleState::Failed;
        return ReplayResult::fail(
            ReplayErrorCode::SnapshotValidationFailed,
            "Failed to inspect snapshot header: " + inspect_res.error_message
        );
    }

    // Single-core execution path guard
    if (snap_info.header.core_count > 1) {
        state_ = ReplayLifecycleState::Failed;
        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core SMP replay is unsupported: snapshot specifies " +
            std::to_string(snap_info.header.core_count) + " cores."
        );
    }

    // Open and validate SNDL log
    cursor_.set_profiler(prof);
    ReplayResult sndl_res{};
    {
        ReplayProfileScope scope(prof ? &prof->sndl_open : nullptr);
        sndl_res = cursor_.open(sndl_path);
    }
    if (sndl_res.is_failure()) {
        state_ = ReplayLifecycleState::Failed;
        return sndl_res;
    }

    // Verify baseline snapshot association if SNDL recorded a non-zero snapshot hash.
    // A recorder may store either the WSnap header CRC32C or the total file size.
    if (config_.require_snapshot_association && cursor_.header().base_snapshot_hash != 0) {
        const std::uint64_t expected_hash = cursor_.header().base_snapshot_hash;
        const std::uint64_t actual_crc = snap_info.header.header_crc32c;
        if (expected_hash != actual_crc &&
            expected_hash != static_cast<std::uint64_t>(snap_info.header.total_file_size)) {
            state_ = ReplayLifecycleState::Failed;
            return ReplayResult::fail(
                ReplayErrorCode::SnapshotMismatch,
                "SNDL base snapshot association does not match the baseline .wsnap"
            );
        }
    }

    state_ = ReplayLifecycleState::RestoringBaseline;

    std::size_t mem_bytes = static_cast<std::size_t>(snap_info.header.memory_capacity);
    if (mem_bytes == 0) {
        mem_bytes = 16 * 1024 * 1024; // 16 MB default fallback
    }
    profile_.memory_capacity = static_cast<std::uint64_t>(mem_bytes);

    // Instantiate clean execution environment
    {
        ReplayProfileScope scope(prof ? &prof->environment_init : nullptr);
        cpu_ = std::make_unique<CPU>();
        memory_ = std::make_unique<Memory>(mem_bytes);
        vt_ = std::make_unique<VirtualTime>();
        gic_ = std::make_unique<GICv3>();

        cpu_->bind_virtual_time(vt_.get(), config_.target_core_id);
    }

    snapshot::SnapshotRestoreContext restore_ctx{};
    restore_ctx.primary_cpu = cpu_.get();
    restore_ctx.memory = memory_.get();
    restore_ctx.virtual_time = vt_.get();
    restore_ctx.gic = gic_.get();

    snapshot::SnapshotResult restore_res{};
    {
        ReplayProfileScope scope(prof ? &prof->snapshot_restore : nullptr);
        restore_res = snapshot::SnapshotManager::restore_from_file(wsnap_path, restore_ctx);
    }
    if (!restore_res.success) {
        state_ = ReplayLifecycleState::Failed;
        return ReplayResult::fail(
            ReplayErrorCode::SnapshotRestoreFailed,
            "Failed to restore architectural state from snapshot: " + restore_res.error_message
        );
    }

    // Optional .wtr diagnostic trace capture
    if (config_.enable_wtr_tracing && !config_.wtr_output_path.empty()) {
        trace::TraceConfig trace_cfg = trace::TraceConfig::default_enabled_config();
        trace_cfg.file_output_enabled = true;
        trace_cfg.output_path = config_.wtr_output_path;
        trace_cfg.core_mask = (1ULL << config_.target_core_id);

        trace_mgr_ = std::make_unique<trace::TraceManager>();
        (void)trace_mgr_->configure(trace_cfg);
        (void)trace_mgr_->start();
        cpu_->bind_trace_manager(trace_mgr_.get());
    }

    // Initialize router and checkpoint manager
    {
        ReplayProfileScope scope(prof ? &prof->routing_init : nullptr);
        router_ = std::make_unique<ReplayInputRouter>(cursor_, "replay_session");
        router_->set_profiler(prof);
        checkpoint_mgr_.reset(config_.checkpoint_interval);
        checkpoint_mgr_.set_profiler(prof);

        // Initialize scheduler
        scheduler_ = std::make_unique<ReplayScheduler>(
            *cpu_, *memory_, vt_.get(), gic_.get(),
            cursor_, *router_, checkpoint_mgr_, config_, prof
        );
    }

    state_ = ReplayLifecycleState::Ready;
    return ReplayResult::ok();
}

ReplayResult ReplaySession::run() {
    auto write_profile_report = [this]() {
        if (!profile_.enabled || config_.profile_report_path.empty()) {
            return;
        }
        ReplayProfileScope scope(&profile_.report_generation);
        (void)ReplayProfiler::write_json(config_.profile_report_path, profile_);
    };

    if (state_ != ReplayLifecycleState::Ready) {
        return ReplayResult::fail(
            ReplayErrorCode::InvalidConfiguration,
            "ReplaySession is not ready for execution (state: " +
            std::string(replay_lifecycle_state_name(state_)) + ")"
        );
    }

    if (smp_coordinator_) {
        state_ = ReplayLifecycleState::Replaying;
        const auto res = smp_coordinator_->run();
        if (smp_coordinator_->state() == SmpReplayState::Completed) {
            state_ = ReplayLifecycleState::Completed;
            stats_.instructions_executed = smp_coordinator_->stats().total_instructions_retired;
            write_profile_report();
            return ReplayResult::ok();
        } else {
            state_ = ReplayLifecycleState::Diverged;
            stats_.diverged = true;
            write_profile_report();
            return res;
        }
    }

    state_ = ReplayLifecycleState::Replaying;
    set_active_replay_router(router_.get());

    const auto res = scheduler_->run(stats_);

    set_active_replay_router(nullptr);

    if (trace_mgr_) {
        (void)trace_mgr_->stop();
    }

    if (stats_.diverged) {
        state_ = ReplayLifecycleState::Diverged;
        // Always emit divergence report if path configured
        if (!config_.divergence_report_path.empty()) {
            (void)ReplayReport::write_json(config_.divergence_report_path, stats_, "replay_session");
        }
        write_profile_report();
        return res;
    }

    state_ = ReplayLifecycleState::Completed;

    if (!config_.divergence_report_path.empty()) {
        (void)ReplayReport::write_json(config_.divergence_report_path, stats_, "replay_session");
    }
    write_profile_report();

    return ReplayResult::ok();
}

bool ReplaySession::step() {
    if (state_ != ReplayLifecycleState::Ready && state_ != ReplayLifecycleState::Replaying) {
        return false;
    }

    if (smp_coordinator_) {
        state_ = ReplayLifecycleState::Replaying;
        const bool stepped = smp_coordinator_->step_quantum();
        if (smp_coordinator_->state() == SmpReplayState::Diverged) {
            state_ = ReplayLifecycleState::Diverged;
            stats_.diverged = true;
        } else if (smp_coordinator_->state() == SmpReplayState::Completed) {
            state_ = ReplayLifecycleState::Completed;
        }
        return stepped;
    }

    state_ = ReplayLifecycleState::Replaying;
    set_active_replay_router(router_.get());

    const bool stepped = scheduler_->step_one(stats_);

    if (stats_.diverged || router_->has_diverged()) {
        state_ = ReplayLifecycleState::Diverged;
        set_active_replay_router(nullptr);
        if (!config_.divergence_report_path.empty()) {
            (void)ReplayReport::write_json(config_.divergence_report_path, stats_, "replay_session");
        }
    }

    return stepped;
}

} // namespace winios::replay
