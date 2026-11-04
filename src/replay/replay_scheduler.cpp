#include "replay/replay_scheduler.h"
#include <chrono>

namespace winios::replay {

ReplayScheduler::ReplayScheduler(
    CPU& cpu, Memory& memory, VirtualTime* vt, GICv3* gic,
    ReplayEventCursor& cursor, ReplayInputRouter& router,
    ReplayCheckpointManager& checkpoint_mgr,
    const ReplayConfig& config,
    ReplayProfile* profile
)
    : cpu_(cpu)
    , memory_(memory)
    , vt_(vt)
    , gic_(gic)
    , cursor_(cursor)
    , router_(router)
    , checkpoint_mgr_(checkpoint_mgr)
    , config_(config)
    , profile_(profile)
{
}

bool ReplayScheduler::inject_pending_asynchronous_inputs() {
    const auto core_id = static_cast<std::uint16_t>(cpu_.get_core_id());
    const auto ret_count = cpu_.get_instruction_retirement_count();

    while (cursor_.has_pending_asynchronous_input(core_id, ret_count)) {
        const auto& rec = cursor_.current();

        if (rec.event_type == sndl::SndlEventType::InterruptDelivery) {
            const auto intid = static_cast<std::uint32_t>(rec.payload1);
            const auto priority = static_cast<std::uint8_t>(rec.payload2 & 0xFFu);
            const bool is_fiq = (rec.payload2 & 0x100ULL) != 0;
            const bool is_virtual = (rec.payload2 & 0x200ULL) != 0;
            const unsigned target_el = static_cast<unsigned>(rec.payload3);

            if (!router_.consume_interrupt_delivery(core_id, rec.pc, ret_count, intid, priority, is_fiq, is_virtual, target_el)) {
                return false;
            }

            if (target_el == 3) {
                cpu_.raise_el3_asynchronous_exception(is_fiq, false, intid, is_virtual, priority);
            } else if (target_el == 2) {
                cpu_.raise_el2_asynchronous_exception(is_fiq, false, intid, is_virtual, priority);
            } else {
                cpu_.raise_asynchronous_exception(is_fiq, intid, is_virtual, priority);
            }
        } else if (rec.event_type == sndl::SndlEventType::TimerExpiration) {
            const auto timer_type = static_cast<std::uint8_t>(rec.payload3);
            const auto cval = rec.payload1;
            const auto counter = rec.payload2;

            if (!router_.consume_timer_expiration(core_id, rec.pc, ret_count, timer_type, cval, counter)) {
                return false;
            }
        } else {
            // Other asynchronous boundary inputs (e.g. host notification)
            (void)cursor_.advance();
        }

        if (router_.has_diverged()) {
            return false;
        }
    }

    return true;
}

bool ReplayScheduler::step_one(ReplayExecutionStats& stats) {
    (void)stats;
    if (router_.has_diverged()) {
        return false;
    }

    {
        ReplayProfileScope injection_scope(profile_ ? &profile_->interrupt_injection : nullptr);
        if (!inject_pending_asynchronous_inputs()) {
            return false;
        }
    }

    if (cpu_.is_halted()) {
        return false;
    }

    if (cpu_.is_waiting()) {
        if (!cursor_.has_next()) {
            return false;
        }
    }

    try {
        ReplayProfileScope cpu_scope(profile_ ? &profile_->cpu_execution : nullptr);
        cpu_.step(memory_);
        ++instructions_executed_;
    } catch (const std::exception& ex) {
        router_.record_divergence(
            ReplayDivergenceCategory::InternalReplayError,
            static_cast<std::uint16_t>(cpu_.get_core_id()),
            cpu_.get_program_counter(),
            cpu_.get_instruction_retirement_count(),
            0,
            sndl::SndlBoundaryType::DuringInstruction,
            sndl::SndlBoundaryType::DuringInstruction,
            sndl::SndlEventType::Unknown,
            sndl::SndlEventType::Unknown,
            0, 0,
            std::string("CPU execution exception during replay: ") + ex.what()
        );
        return false;
    }

    if (router_.has_diverged()) {
        return false;
    }

    checkpoint_mgr_.maybe_record_checkpoint(
        cpu_, memory_, vt_, gic_, cursor_.last_consumed_sequence()
    );

    return true;
}

ReplayResult ReplayScheduler::run(ReplayExecutionStats& stats) {
    ReplayProfileScope run_scope(profile_ ? &profile_->run_total : nullptr);
    const auto start_time = std::chrono::steady_clock::now();

    // Record initial baseline checkpoint
    checkpoint_mgr_.force_checkpoint(cpu_, memory_, vt_, gic_, 0);

    std::uint64_t target_instructions = config_.max_instructions;
    if (target_instructions == 0) {
        if (cursor_.header().total_instructions > 0) {
            target_instructions = cursor_.header().total_instructions;
        } else if (cursor_.is_footer_read() && cursor_.footer().total_instructions > 0) {
            target_instructions = cursor_.footer().total_instructions;
        }
    }

    while (true) {
        if (router_.has_diverged()) {
            break;
        }

        if (target_instructions > 0 && instructions_executed_ >= target_instructions) {
            break;
        }

        if (config_.max_replay_duration_ms > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            if (static_cast<std::uint64_t>(elapsed_ms) >= config_.max_replay_duration_ms) {
                router_.record_divergence(
                    ReplayDivergenceCategory::ReplayTimeout,
                    static_cast<std::uint16_t>(cpu_.get_core_id()),
                    cpu_.get_program_counter(),
                    cpu_.get_instruction_retirement_count(),
                    0,
                    sndl::SndlBoundaryType::Unknown,
                    sndl::SndlBoundaryType::Unknown,
                    sndl::SndlEventType::Unknown,
                    sndl::SndlEventType::Unknown,
                    0, 0,
                    "Replay exceeded maximum duration limit of " +
                    std::to_string(config_.max_replay_duration_ms) + " ms"
                );
                break;
            }
        }

        if (target_instructions == 0 && !cursor_.has_next() && (cpu_.is_halted() || cpu_.is_waiting())) {
            break;
        }

        if (!step_one(stats)) {
            break;
        }
    }

    // Force final checkpoint upon completion
    checkpoint_mgr_.force_checkpoint(
        cpu_, memory_, vt_, gic_, cursor_.last_consumed_sequence()
    );

    const auto end_time = std::chrono::steady_clock::now();
    stats.elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    stats.instructions_executed = instructions_executed_;
    stats.events_consumed = cursor_.total_consumed();
    stats.checkpoints_taken = checkpoint_mgr_.count();
    {
        ReplayProfileScope hash_scope(profile_ ? &profile_->final_hash : nullptr);
        stats.final_state_hashes = ReplayStateHash::compute_state_hashes(
            cpu_, memory_, vt_, gic_, profile_);
    }

    auto sync_profile = [&]() {
        if (profile_ == nullptr) {
            return;
        }
        profile_->instructions_executed = stats.instructions_executed;
        profile_->events_consumed = stats.events_consumed;
        profile_->checkpoints_recorded = stats.checkpoints_taken;
        profile_->completion_status = stats.completion_status;
        profile_->diverged = stats.diverged;
    };

    if (router_.has_diverged()) {
        ReplayProfileScope completion_scope(
            profile_ ? &profile_->completion_validation : nullptr);
        stats.diverged = true;
        stats.divergence_info = router_.divergence();
        stats.divergence_info.actual_hashes = stats.final_state_hashes;
        if (stats.divergence_info.category == ReplayDivergenceCategory::ReplayTimeout) {
            stats.completion_status = ReplayCompletionStatus::TimedOut;
        } else {
            stats.completion_status = ReplayCompletionStatus::Diverged;
        }
        sync_profile();
        return ReplayResult::fail(ReplayErrorCode::Diverged, stats.divergence_info.message);
    }

    stats.diverged = false;

    // Stage 20: Audit event accounting and completion status
    {
        ReplayProfileScope completion_scope(
            profile_ ? &profile_->completion_validation : nullptr);
        if (cursor_.has_next()) {
            if (config_.require_exact_event_consumption) {
                router_.record_divergence(
                    ReplayDivergenceCategory::EventSequenceMismatch,
                    static_cast<std::uint16_t>(cpu_.get_core_id()),
                    cpu_.get_program_counter(),
                    cpu_.get_instruction_retirement_count(),
                    0,
                    sndl::SndlBoundaryType::Unknown,
                    sndl::SndlBoundaryType::Unknown,
                    cursor_.current().event_type,
                    sndl::SndlEventType::Unknown,
                    cursor_.current().sequence_num,
                    0,
                    "Replay finished with unconsumed events remaining in SNDL stream"
                );
                stats.diverged = true;
                stats.divergence_info = router_.divergence();
                stats.divergence_info.actual_hashes = stats.final_state_hashes;
                stats.completion_status = ReplayCompletionStatus::UnconsumedEvents;
                sync_profile();
                return ReplayResult::fail(ReplayErrorCode::Diverged, stats.divergence_info.message);
            }
            stats.completion_status = ReplayCompletionStatus::UnconsumedEvents;
        } else if (cpu_.is_halted() || cpu_.is_waiting()) {
            stats.completion_status = ReplayCompletionStatus::CompletedAtGuestHalt;
        } else if (config_.max_instructions > 0 && (cursor_.header().total_instructions == 0 || config_.max_instructions < cursor_.header().total_instructions)) {
            stats.completion_status = ReplayCompletionStatus::CompletedAtConfiguredInstructionLimit;
        } else {
            stats.completion_status = ReplayCompletionStatus::CompletedNormally;
        }
    }

    sync_profile();
    return ReplayResult::ok();
}

} // namespace winios::replay
