#include "replay/replay_checkpoint.h"

namespace winios::replay {

ReplayCheckpointManager::ReplayCheckpointManager(std::uint64_t interval)
    : interval_(interval) {
    checkpoints_.reserve(32);
}

void ReplayCheckpointManager::reset(std::uint64_t interval) noexcept {
    interval_ = interval;
    last_checkpoint_ret_ = 0;
    checkpoints_.clear();
}

bool ReplayCheckpointManager::maybe_record_checkpoint(
    const CPU& cpu,
    const Memory& memory,
    const VirtualTime* vt,
    const GICv3* gic,
    std::uint64_t current_seq
) {
    if (interval_ == 0) {
        return false;
    }

    const std::uint64_t ret = cpu.get_instruction_retirement_count();
    if (ret > 0 && ret >= last_checkpoint_ret_ + interval_) {
        force_checkpoint(cpu, memory, vt, gic, current_seq);
        last_checkpoint_ret_ = ret;
        return true;
    }

    return false;
}

void ReplayCheckpointManager::force_checkpoint(
    const CPU& cpu,
    const Memory& memory,
    const VirtualTime* vt,
    const GICv3* gic,
    std::uint64_t current_seq
) {
    ReplayProfileScope total_scope(profiler_ ? &profiler_->checkpoint_total : nullptr);

    ReplayCheckpoint cp{};
    cp.instruction_retirement_count = cpu.get_instruction_retirement_count();
    cp.pc = cpu.get_program_counter();
    cp.sequence_num = current_seq;
    {
        ReplayProfileScope hash_scope(profiler_ ? &profiler_->checkpoint_hash : nullptr);
        cp.hashes = ReplayStateHash::compute_state_hashes(cpu, memory, vt, gic, profiler_);
    }

    checkpoints_.push_back(cp);
    last_checkpoint_ret_ = cp.instruction_retirement_count;
    if (profiler_ != nullptr) {
        ++profiler_->checkpoints_recorded;
    }
}

} // namespace winios::replay
