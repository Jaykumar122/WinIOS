#include "replay/replay_input_router.h"

namespace winios::replay {

namespace {
std::atomic<ReplayInputRouter*> g_active_router{nullptr};
} // namespace

void set_active_replay_router(ReplayInputRouter* router) noexcept {
    g_active_router.store(router, std::memory_order_release);
}

ReplayInputRouter* get_active_replay_router() noexcept {
    return g_active_router.load(std::memory_order_acquire);
}

bool is_replay_active() noexcept {
    return g_active_router.load(std::memory_order_acquire) != nullptr;
}

ReplayInputRouter::ReplayInputRouter(ReplayEventCursor& cursor, std::string session_id)
    : cursor_(cursor), session_id_(std::move(session_id)) {
}

ReplayInputRouter::~ReplayInputRouter() {
    if (get_active_replay_router() == this) {
        set_active_replay_router(nullptr);
    }
}

void ReplayInputRouter::record_divergence(
    ReplayDivergenceCategory category,
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, std::uint16_t sub_ord,
    sndl::SndlBoundaryType exp_bnd, sndl::SndlBoundaryType act_bnd,
    sndl::SndlEventType exp_ev, sndl::SndlEventType act_ev,
    std::uint64_t exp_val, std::uint64_t act_val,
    std::string message
) {
    if (divergence_.has_diverged()) {
        return; // Retain the first confirmed divergence
    }

    divergence_.category = category;
    divergence_.session_id = session_id_;
    divergence_.sequence_num = cursor_.has_next() ? cursor_.current().sequence_num : cursor_.last_consumed_sequence() + 1;
    divergence_.core_id = core_id;
    divergence_.pc = pc;
    divergence_.retirement_count = ret_count;
    divergence_.sub_instruction_ordinal = sub_ord;
    divergence_.expected_boundary = exp_bnd;
    divergence_.actual_boundary = act_bnd;
    divergence_.expected_event_type = exp_ev;
    divergence_.actual_event_type = act_ev;
    divergence_.expected_value = exp_val;
    divergence_.actual_value = act_val;
    divergence_.message = std::move(message);
}

bool ReplayInputRouter::consume_mmio_read(
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, std::uint64_t paddr,
    unsigned width, std::uint64_t& out_value
) {
    ReplayProfileScope route_scope(profiler_ ? &profiler_->input_routing : nullptr);
    if (profiler_ != nullptr) {
        ++profiler_->input_route_attempts;
    }
    if (!cursor_.has_next()) {
        record_divergence(
            ReplayDivergenceCategory::InputValueMismatch,
            core_id, pc, ret_count, 0,
            sndl::SndlBoundaryType::Unknown, sndl::SndlBoundaryType::DuringInstruction,
            sndl::SndlEventType::Unknown, sndl::SndlEventType::MmioReadResult,
            0, paddr,
            "Execution requested MMIO read but SNDL stream is exhausted"
        );
        return false;
    }

    const auto& rec = cursor_.current();

    if (rec.event_type != sndl::SndlEventType::MmioReadResult) {
        record_divergence(
            ReplayDivergenceCategory::EventTypeMismatch,
            core_id, pc, ret_count, 0,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::MmioReadResult,
            rec.payload1, paddr,
            "Expected event " + std::string(sndl::sndl_event_type_name(rec.event_type)) +
            " but execution attempted MMIO read"
        );
        return false;
    }

    if (rec.core_id != core_id) {
        record_divergence(
            ReplayDivergenceCategory::CoreMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::MmioReadResult,
            rec.core_id, core_id,
            "MMIO read core mismatch: expected core " + std::to_string(rec.core_id) +
            ", actual core " + std::to_string(core_id)
        );
        return false;
    }

    if (rec.payload1 != paddr) {
        record_divergence(
            ReplayDivergenceCategory::MMIOAddressMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::MmioReadResult,
            rec.payload1, paddr,
            "MMIO read address mismatch"
        );
        return false;
    }

    if (rec.payload3 != width) {
        record_divergence(
            ReplayDivergenceCategory::MMIOWidthMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::MmioReadResult,
            rec.payload3, width,
            "MMIO read access width mismatch"
        );
        return false;
    }

    // Match confirmed! Supply recorded value and advance cursor
    out_value = rec.payload2;
    (void)cursor_.advance();
    if (profiler_ != nullptr) {
        ++profiler_->input_routes_consumed;
    }
    return true;
}

bool ReplayInputRouter::consume_counter_read(
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, std::uint32_t reg_key,
    std::uint64_t& out_value
) {
    ReplayProfileScope route_scope(profiler_ ? &profiler_->input_routing : nullptr);
    if (profiler_ != nullptr) {
        ++profiler_->input_route_attempts;
    }
    if (!cursor_.has_next()) {
        record_divergence(
            ReplayDivergenceCategory::InputValueMismatch,
            core_id, pc, ret_count, 0,
            sndl::SndlBoundaryType::Unknown, sndl::SndlBoundaryType::DuringInstruction,
            sndl::SndlEventType::Unknown, sndl::SndlEventType::CounterReadResult,
            0, reg_key,
            "Execution requested counter read but SNDL stream is exhausted"
        );
        return false;
    }

    const auto& rec = cursor_.current();
    if (rec.event_type != sndl::SndlEventType::CounterReadResult) {
        record_divergence(
            ReplayDivergenceCategory::EventTypeMismatch,
            core_id, pc, ret_count, 0,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::CounterReadResult,
            0, reg_key,
            "Expected event " + std::string(sndl::sndl_event_type_name(rec.event_type)) +
            " but execution attempted counter read"
        );
        return false;
    }

    if (rec.core_id != core_id) {
        record_divergence(
            ReplayDivergenceCategory::CoreMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::CounterReadResult,
            rec.core_id, core_id,
            "Counter read core mismatch: expected core " + std::to_string(rec.core_id) +
            ", actual core " + std::to_string(core_id)
        );
        return false;
    }

    if (rec.instruction_retirement_count != ret_count) {
        record_divergence(
            ReplayDivergenceCategory::RetirementMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::CounterReadResult,
            rec.instruction_retirement_count, ret_count,
            "Counter read instruction retirement count mismatch"
        );
        return false;
    }

    if (rec.payload1 != reg_key) {
        record_divergence(
            ReplayDivergenceCategory::TimerMismatch,
            core_id, pc, ret_count, rec.sub_instruction_ordinal,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::CounterReadResult,
            rec.payload1, reg_key,
            "Counter read register key mismatch"
        );
        return false;
    }

    out_value = rec.payload2;
    (void)cursor_.advance();
    if (profiler_ != nullptr) {
        ++profiler_->input_routes_consumed;
    }
    return true;
}

bool ReplayInputRouter::consume_entropy(
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, unsigned width,
    std::uint64_t& out_value
) {
    ReplayProfileScope route_scope(profiler_ ? &profiler_->input_routing : nullptr);
    if (profiler_ != nullptr) {
        ++profiler_->input_route_attempts;
    }
    (void)width;
    if (!cursor_.has_next()) {
        record_divergence(
            ReplayDivergenceCategory::InputValueMismatch,
            core_id, pc, ret_count, 0,
            sndl::SndlBoundaryType::Unknown, sndl::SndlBoundaryType::DuringInstruction,
            sndl::SndlEventType::Unknown, sndl::SndlEventType::EntropyResult,
            0, 0,
            "Execution requested entropy but SNDL log has no entropy event"
        );
        return false;
    }

    const auto& rec = cursor_.current();
    if (rec.event_type != sndl::SndlEventType::EntropyResult) {
        record_divergence(
            ReplayDivergenceCategory::EventTypeMismatch,
            core_id, pc, ret_count, 0,
            rec.boundary_type, sndl::SndlBoundaryType::DuringInstruction,
            rec.event_type, sndl::SndlEventType::EntropyResult,
            0, 0,
            "Expected event " + std::string(sndl::sndl_event_type_name(rec.event_type)) +
            " but execution requested entropy"
        );
        return false;
    }

    out_value = rec.payload1;
    (void)cursor_.advance();
    if (profiler_ != nullptr) {
        ++profiler_->input_routes_consumed;
    }
    return true;
}

bool ReplayInputRouter::consume_interrupt_delivery(
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, std::uint32_t intid,
    std::uint8_t priority, bool is_fiq,
    bool is_virtual, unsigned target_el
) {
    ReplayProfileScope route_scope(profiler_ ? &profiler_->input_routing : nullptr);
    if (profiler_ != nullptr) {
        ++profiler_->input_route_attempts;
    }
    (void)priority;
    (void)is_fiq;
    (void)is_virtual;
    (void)target_el;
    if (!cursor_.has_next()) {
        return false;
    }

    const auto& rec = cursor_.current();
    if (rec.event_type == sndl::SndlEventType::InterruptDelivery &&
        rec.core_id == core_id &&
        rec.instruction_retirement_count == ret_count) {
        if (rec.payload1 != intid) {
            record_divergence(
                ReplayDivergenceCategory::InterruptMismatch,
                core_id, pc, ret_count, rec.sub_instruction_ordinal,
                rec.boundary_type, sndl::SndlBoundaryType::ExceptionBoundary,
                rec.event_type, sndl::SndlEventType::InterruptDelivery,
                rec.payload1, intid,
                "Accepted interrupt ID mismatch"
            );
            return false;
        }

        (void)cursor_.advance();
        if (profiler_ != nullptr) {
            ++profiler_->input_routes_consumed;
        }
        return true;
    }

    return false;
}

bool ReplayInputRouter::consume_timer_expiration(
    std::uint16_t core_id, std::uint64_t pc,
    std::uint64_t ret_count, std::uint8_t timer_type,
    std::uint64_t cval, std::uint64_t counter
) {
    ReplayProfileScope route_scope(profiler_ ? &profiler_->input_routing : nullptr);
    if (profiler_ != nullptr) {
        ++profiler_->input_route_attempts;
    }
    (void)pc;
    (void)timer_type;
    (void)cval;
    (void)counter;
    if (!cursor_.has_next()) {
        return false;
    }

    const auto& rec = cursor_.current();
    if (rec.event_type == sndl::SndlEventType::TimerExpiration &&
        rec.core_id == core_id &&
        rec.instruction_retirement_count == ret_count) {
        (void)cursor_.advance();
        if (profiler_ != nullptr) {
            ++profiler_->input_routes_consumed;
        }
        return true;
    }

    return false;
}

} // namespace winios::replay
