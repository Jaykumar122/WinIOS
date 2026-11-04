#include "cpu/smp_scheduler.h"
#include "sndl/sndl_v2_recorder.h"
#include "trace/trace_serializer.h"

#include <stdexcept>

namespace winios {

namespace {

constexpr std::uint64_t kFnv1aOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnv1aPrime = 0x100000001b3ULL;

inline std::uint64_t fnv1a_bytes(std::uint64_t hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnv1aPrime;
    }
    return hash;
}

inline std::uint64_t fnv1a_u64(std::uint64_t hash, std::uint64_t val) noexcept {
    std::uint8_t b[8];
    trace::encode_u64_le(b, val);
    return fnv1a_bytes(hash, b, 8);
}

inline std::uint64_t fnv1a_u32(std::uint64_t hash, std::uint32_t val) noexcept {
    std::uint8_t b[4];
    trace::encode_u32_le(b, val);
    return fnv1a_bytes(hash, b, 4);
}

} // namespace

SmpScheduler::SmpScheduler(std::shared_ptr<SmpSharedMemory> shared_memory,
                           std::size_t max_cores,
                           std::uint64_t quantum_instructions)
    : shared_memory_(std::move(shared_memory)),
      exclusive_monitor_(max_cores),
      interrupt_router_(max_cores),
      slots_(max_cores),
      quantum_instructions_(quantum_instructions) {
    if (!shared_memory_) {
        throw std::invalid_argument("shared_memory cannot be null in SmpScheduler");
    }
    shared_memory_->attach_exclusive_monitor(&exclusive_monitor_);
}

void SmpScheduler::register_core(std::size_t core_id, std::shared_ptr<CPU> cpu,
                                 CoreLifecycleState initial_state) {
    if (!cpu) {
        throw std::invalid_argument("cpu cannot be null in SmpScheduler::register_core");
    }
    if (core_id >= slots_.size()) {
        slots_.resize(core_id + 1);
    }

    auto& slot = slots_[core_id];
    slot.core_id = core_id;
    slot.cpu = cpu;
    slot.active = true;
    slot.total_instructions_retired = 0;
    slot.quantums_allocated = 0;
    slot.early_truncations = 0;

    const std::uint64_t initial_pc = cpu->get_program_counter();
    slot.cpu->set_core_id(core_id);
    slot.state.reset(core_id, initial_state);
    slot.state.pc = initial_pc;
    slot.state.apply_to_cpu(*slot.cpu);

    registered_core_count_ = 0;
    for (const auto& s : slots_) {
        if (s.active) ++registered_core_count_;
    }
}

const SmpSchedulerSlot& SmpScheduler::get_slot(std::size_t core_id) const {
    if (core_id >= slots_.size() || !slots_[core_id].active) {
        throw std::out_of_range("core_id is not registered in SmpScheduler");
    }
    return slots_[core_id];
}

SmpSchedulerSlot& SmpScheduler::get_slot(std::size_t core_id) {
    if (core_id >= slots_.size() || !slots_[core_id].active) {
        throw std::out_of_range("core_id is not registered in SmpScheduler");
    }
    return slots_[core_id];
}

QuantumTransitionRecord SmpScheduler::step_quantum() {
    if (registered_core_count_ == 0 || slots_.empty()) {
        QuantumTransitionRecord rec;
        rec.core_id = 0;
        rec.start_global_sequence = global_retirement_sequence_;
        rec.end_global_sequence = global_retirement_sequence_;
        rec.instructions_retired = 0;
        rec.reason = QuantumTruncationReason::AllCoresQuiescent;
        return rec;
    }

    // 1. Round-robin search for next runnable core
    std::size_t selected_core = slots_.size();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const std::size_t candidate_idx = (current_core_index_ + i) % slots_.size();
        auto& slot = slots_[candidate_idx];
        if (!slot.active) continue;

        if (slot.cpu->is_stopped() || slot.cpu->is_halted()) {
            continue;
        }

        if (slot.cpu->is_waiting_for_interrupt()) {
            if (interrupt_router_.has_pending(candidate_idx)) {
                // Wake quiescent core and deliver pending interrupt
                interrupt_router_.deliver_next(candidate_idx, *slot.cpu);
                slot.cpu->wake_from_wfi();
                slot.state.lifecycle = CoreLifecycleState::Runnable;
                selected_core = candidate_idx;
                break;
            }
            continue;
        }

        if (slot.cpu->is_waiting_for_event()) {
            if (slot.cpu->get_event_register()) {
                slot.cpu->wake_from_wfe();
                slot.state.lifecycle = CoreLifecycleState::Runnable;
                selected_core = candidate_idx;
                break;
            }
            continue;
        }

        selected_core = candidate_idx;
        break;
    }

    if (selected_core >= slots_.size()) {
        QuantumTransitionRecord rec;
        rec.core_id = 0;
        rec.start_global_sequence = global_retirement_sequence_;
        rec.end_global_sequence = global_retirement_sequence_;
        rec.instructions_retired = 0;
        rec.reason = QuantumTruncationReason::AllCoresQuiescent;
        return rec;
    }

    // Advance round-robin index for next selection
    current_core_index_ = (selected_core + 1) % slots_.size();

    auto& slot = slots_[selected_core];
    slot.quantums_allocated++;

    const std::uint64_t start_seq = global_retirement_sequence_;
    std::uint64_t executed = 0;
    QuantumTruncationReason reason = QuantumTruncationReason::QuantumExhausted;

    if (sndl_recorder_) {
        sndl_recorder_->record_core_selected(selected_core, 0, slot.cpu->get_program_counter());
        sndl_recorder_->record_quantum_start(selected_core, quantum_instructions_, slot.cpu->get_program_counter());
    }

    // 2. Execute up to Q instructions within this quantum
    for (std::uint64_t q = 0; q < quantum_instructions_; ++q) {
        if (slot.cpu->is_halted()) {
            reason = QuantumTruncationReason::CoreHalted;
            break;
        }
        if (slot.cpu->is_waiting_for_interrupt()) {
            reason = QuantumTruncationReason::CoreWaitingForInterrupt;
            break;
        }
        if (slot.cpu->is_waiting_for_event()) {
            reason = QuantumTruncationReason::CoreWaitingForEvent;
            break;
        }

        slot.cpu->set_exception_taken_in_step(false);

        // Architectural execution of one instruction
        slot.cpu->step(shared_memory_->raw_memory());

        if (slot.cpu->exception_taken_in_step()) {
            reason = QuantumTruncationReason::ExceptionRaised;
            if (sndl_recorder_) {
                sndl_recorder_->record_exception_taken(selected_core, 0, static_cast<std::uint32_t>(slot.cpu->get_current_el()), slot.cpu->get_program_counter());
            }
            break;
        }

        executed++;
        global_retirement_sequence_++;
        if (sndl_recorder_) {
            sndl_recorder_->notify_instruction_retired(selected_core, 1);
        }
        if (slot.cpu->is_halted()) {
            reason = QuantumTruncationReason::CoreHalted;
            break;
        }
        if (slot.cpu->is_waiting_for_interrupt()) {
            reason = QuantumTruncationReason::CoreWaitingForInterrupt;
            break;
        }
        if (slot.cpu->is_waiting_for_event()) {
            reason = QuantumTruncationReason::CoreWaitingForEvent;
            break;
        }
    }

    slot.total_instructions_retired += executed;
    slot.state.capture_from_cpu(*slot.cpu);

    if (reason != QuantumTruncationReason::QuantumExhausted) {
        slot.early_truncations++;
    }

    if (sndl_recorder_) {
        sndl_recorder_->record_quantum_end(selected_core, executed, static_cast<std::uint32_t>(reason), slot.cpu->get_program_counter());
        if (reason != QuantumTruncationReason::QuantumExhausted) {
            sndl_recorder_->record_early_truncation(selected_core, static_cast<std::uint32_t>(reason), slot.cpu->get_program_counter());
        }
    }

    QuantumTransitionRecord record;
    record.core_id = selected_core;
    record.start_global_sequence = start_seq;
    record.end_global_sequence = global_retirement_sequence_;
    record.instructions_retired = executed;
    record.reason = reason;

    history_.push_back(record);
    return record;
}

std::uint64_t SmpScheduler::run_deterministic(std::uint64_t max_total_instructions) {
    std::uint64_t total_retired = 0;

    while (total_retired < max_total_instructions) {
        const auto rec = step_quantum();
        if (rec.reason == QuantumTruncationReason::AllCoresQuiescent) {
            break;
        }
        total_retired += rec.instructions_retired;
    }

    return total_retired;
}

std::uint64_t SmpScheduler::run_deterministic_ticks(std::size_t num_ticks) {
    std::uint64_t total_retired = 0;

    for (std::size_t i = 0; i < num_ticks; ++i) {
        const auto rec = step_quantum();
        if (rec.reason == QuantumTruncationReason::AllCoresQuiescent) {
            break;
        }
        total_retired += rec.instructions_retired;
    }

    return total_retired;
}

void SmpScheduler::reset() {
    global_retirement_sequence_ = 0;
    current_core_index_ = 0;
    history_.clear();
    exclusive_monitor_.reset();
    interrupt_router_.clear_all();

    for (auto& slot : slots_) {
        if (!slot.active) continue;
        slot.total_instructions_retired = 0;
        slot.quantums_allocated = 0;
        slot.early_truncations = 0;
        slot.state.reset(slot.core_id, CoreLifecycleState::Runnable);
        if (slot.cpu) {
            slot.state.apply_to_cpu(*slot.cpu);
        }
    }
}

std::uint64_t SmpScheduler::compute_canonical_multicore_hash() const noexcept {
    std::uint64_t h = kFnv1aOffsetBasis;

    h = fnv1a_u64(h, static_cast<std::uint64_t>(registered_core_count_));
    h = fnv1a_u64(h, global_retirement_sequence_);

    // 1. Hash each active core state in strict ascending core_id order
    for (const auto& slot : slots_) {
        if (!slot.active) continue;
        const std::uint64_t core_hash = slot.state.compute_canonical_hash();
        h = fnv1a_u64(h, static_cast<std::uint64_t>(slot.core_id));
        h = fnv1a_u64(h, core_hash);
        h = fnv1a_u64(h, slot.total_instructions_retired);
    }

    // 2. Hash shared guest RAM contents
    if (shared_memory_) {
        const auto* raw = shared_memory_->raw_memory().raw_data();
        const std::size_t sz = shared_memory_->size();
        const std::uint32_t mem_crc = trace::calculate_crc32c(raw, sz);
        h = fnv1a_u32(h, mem_crc);
    }

    return h;
}

} // namespace winios
