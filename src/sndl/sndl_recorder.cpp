#include "sndl/sndl_recorder.h"
#include "sndl/sndl_event_encoder.h"

#include <chrono>

namespace winios::sndl {

SndlRecorder::SndlRecorder(std::size_t queue_capacity)
    : queue_capacity_(queue_capacity) {
}

SndlRecorder::~SndlRecorder() {
    abort_recording();
}

SndlResult SndlRecorder::start_recording(const std::filesystem::path& output_path,
                                         std::uint16_t core_count,
                                         std::uint64_t base_snapshot_hash,
                                         const std::array<std::uint8_t, 12>& snapshot_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (state_.load(std::memory_order_acquire) != SndlLifecycleState::Idle &&
        state_.load(std::memory_order_acquire) != SndlLifecycleState::Completed &&
        state_.load(std::memory_order_acquire) != SndlLifecycleState::Failed) {
        return SndlResult::fail(SndlErrorCode::InvalidStateTransition, "Recorder already active");
    }

    state_.store(SndlLifecycleState::Starting, std::memory_order_release);
    overflowed_.store(false, std::memory_order_release);
    sequence_counter_.store(0, std::memory_order_relaxed);
    total_recorded_.store(0, std::memory_order_relaxed);
    dropped_records_.store(0, std::memory_order_relaxed);
    core_count_ = core_count > 0 ? core_count : 1;

    {
        std::lock_guard<std::mutex> core_lock(per_core_mutex_);
        core_tracking_.assign(core_count_, PerCoreTracking{});
    }

    WSndlHeader hdr{};
    hdr.magic = kWSndlMagic;
    hdr.version = kWSndlCurrentVersion;
    hdr.core_count = core_count_;
    hdr.flags = 0;
    hdr.base_snapshot_hash = base_snapshot_hash;
    hdr.snapshot_id = snapshot_id;
    hdr.creation_timestamp_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    hdr.total_record_count = 0;
    hdr.total_instructions = 0;

    const auto open_res = writer_.open(output_path, hdr);
    if (open_res.is_failure()) {
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
        return open_res;
    }

    state_.store(SndlLifecycleState::Recording, std::memory_order_release);
    return SndlResult::ok();
}

SndlResult SndlRecorder::stop_recording(std::uint64_t total_instructions) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (state_.load(std::memory_order_acquire) != SndlLifecycleState::Recording &&
        state_.load(std::memory_order_acquire) != SndlLifecycleState::Failed) {
        return SndlResult::ok();
    }

    state_.store(SndlLifecycleState::Stopping, std::memory_order_release);
    state_.store(SndlLifecycleState::Flushing, std::memory_order_release);

    const bool had_overflow = overflowed_.load(std::memory_order_acquire);
    const SndlFinalizationStatus final_status = had_overflow
        ? SndlFinalizationStatus::Overflow
        : SndlFinalizationStatus::Completed;

    state_.store(SndlLifecycleState::Finalizing, std::memory_order_release);
    const auto res = writer_.finalize(final_status, total_instructions);

    if (had_overflow) {
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
        return SndlResult::fail(SndlErrorCode::BufferOverflow,
                                "Recording aborted due to buffer overflow (" +
                                std::to_string(dropped_records_.load(std::memory_order_relaxed)) + " dropped)");
    }

    if (res.is_failure()) {
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
        return res;
    }

    state_.store(SndlLifecycleState::Completed, std::memory_order_release);
    return SndlResult::ok();
}

void SndlRecorder::abort_recording() noexcept {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (state_.load(std::memory_order_acquire) == SndlLifecycleState::Recording) {
        (void)writer_.finalize(SndlFinalizationStatus::Aborted, 0);
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
    }
}

SndlResult SndlRecorder::submit_record(SndlRecord record) {
    if (!is_recording()) {
        return SndlResult::ok(); // Silently ignore if recording is inactive
    }

    // Sequence assignment
    record.sequence_num = ++sequence_counter_;

    // Sub-instruction ordinal tracking
    {
        std::lock_guard<std::mutex> core_lock(per_core_mutex_);
        if (record.core_id < core_tracking_.size()) {
            auto& trk = core_tracking_[record.core_id];
            if (record.instruction_retirement_count == trk.last_ret_count && trk.last_ret_count != 0) {
                record.sub_instruction_ordinal = trk.next_sub_ordinal++;
            } else {
                trk.last_ret_count = record.instruction_retirement_count;
                trk.next_sub_ordinal = 1;
                record.sub_instruction_ordinal = 0;
            }
        }
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Check bounded capacity
    if (overflowed_.load(std::memory_order_relaxed) ||
        (queue_capacity_ > 0 && total_recorded_.load(std::memory_order_relaxed) >= queue_capacity_)) {
        overflowed_.store(true, std::memory_order_release);
        dropped_records_.fetch_add(1, std::memory_order_relaxed);
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
        return SndlResult::fail(SndlErrorCode::BufferOverflow, "Recording buffer overflowed; dropped nondeterministic event");
    }

    const auto write_res = writer_.write_record(record);
    if (write_res.is_failure()) {
        state_.store(SndlLifecycleState::Failed, std::memory_order_release);
        return write_res;
    }

    total_recorded_.fetch_add(1, std::memory_order_relaxed);
    return SndlResult::ok();
}

void SndlRecorder::record_mmio_read(std::uint16_t core_id, std::uint64_t pc,
                                    std::uint64_t ret_count, std::uint64_t paddr,
                                    std::uint64_t value, unsigned width,
                                    std::uint32_t source_id) {
    if (!is_recording()) return;
    auto rec = make_mmio_read_record(core_id, pc, ret_count, 0, paddr, value, width, source_id);
    (void)submit_record(rec);
}

void SndlRecorder::record_interrupt_inject(std::uint16_t core_id, std::uint32_t intid,
                                          std::uint8_t priority, std::uint32_t flags,
                                          std::uint32_t source_id) {
    if (!is_recording()) return;
    auto rec = make_interrupt_inject_record(core_id, intid, priority, flags, source_id);
    (void)submit_record(rec);
}

void SndlRecorder::record_interrupt_delivery(std::uint16_t core_id, std::uint64_t pc,
                                             std::uint64_t ret_count, std::uint32_t intid,
                                             std::uint8_t priority, bool is_fiq,
                                             bool is_virtual, unsigned target_el) {
    if (!is_recording()) return;
    auto rec = make_interrupt_delivery_record(core_id, pc, ret_count, intid, priority, is_fiq, is_virtual, target_el);
    (void)submit_record(rec);
}

void SndlRecorder::record_timer_expiration(std::uint16_t core_id, std::uint64_t pc,
                                          std::uint64_t ret_count, std::uint8_t timer_type,
                                          std::uint64_t cval, std::uint64_t counter) {
    if (!is_recording()) return;
    auto rec = make_timer_expiration_record(core_id, pc, ret_count, timer_type, cval, counter);
    (void)submit_record(rec);
}

void SndlRecorder::record_counter_read(std::uint16_t core_id, std::uint64_t pc,
                                      std::uint64_t ret_count, std::uint32_t reg_key,
                                      std::uint64_t counter_val, std::uint64_t frequency) {
    if (!is_recording()) return;
    auto rec = make_counter_read_record(core_id, pc, ret_count, 0, reg_key, counter_val, frequency);
    (void)submit_record(rec);
}

void SndlRecorder::record_entropy(std::uint16_t core_id, std::uint64_t pc,
                                 std::uint64_t ret_count, std::uint64_t value,
                                 unsigned bit_width, std::uint32_t source_id) {
    if (!is_recording()) return;
    auto rec = make_entropy_record(core_id, pc, ret_count, 0, value, bit_width, source_id);
    (void)submit_record(rec);
}

void SndlRecorder::record_host_input(std::uint16_t core_id, std::uint32_t channel_id,
                                    std::uint64_t data_payload, std::size_t length) {
    if (!is_recording()) return;
    auto rec = make_host_input_record(core_id, channel_id, data_payload, length);
    (void)submit_record(rec);
}

} // namespace winios::sndl
