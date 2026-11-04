#pragma once

#include "replay/replay_divergence.h"
#include "replay/replay_event_cursor.h"
#include "replay/replay_profiler.h"
#include "replay/replay_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayInputRouter: Injects Logged Values at Supported Architectural Boundaries
// -----------------------------------------------------------------------------
class ReplayInputRouter {
public:
    explicit ReplayInputRouter(ReplayEventCursor& cursor, std::string session_id = "replay_session");
    ~ReplayInputRouter();

    ReplayInputRouter(const ReplayInputRouter&) = delete;
    ReplayInputRouter& operator=(const ReplayInputRouter&) = delete;

    // Stage 21: Optional profile sink. Null disables timing and counters.
    void set_profiler(ReplayProfile* profile) noexcept { profiler_ = profile; }

    // --- Input Consumption & Verification ---
    bool consume_mmio_read(std::uint16_t core_id, std::uint64_t pc,
                           std::uint64_t ret_count, std::uint64_t paddr,
                           unsigned width, std::uint64_t& out_value);

    bool consume_counter_read(std::uint16_t core_id, std::uint64_t pc,
                              std::uint64_t ret_count, std::uint32_t reg_key,
                              std::uint64_t& out_value);

    bool consume_entropy(std::uint16_t core_id, std::uint64_t pc,
                         std::uint64_t ret_count, unsigned width,
                         std::uint64_t& out_value);

    bool consume_interrupt_delivery(std::uint16_t core_id, std::uint64_t pc,
                                    std::uint64_t ret_count, std::uint32_t intid,
                                    std::uint8_t priority, bool is_fiq,
                                    bool is_virtual, unsigned target_el);

    bool consume_timer_expiration(std::uint16_t core_id, std::uint64_t pc,
                                  std::uint64_t ret_count, std::uint8_t timer_type,
                                  std::uint64_t cval, std::uint64_t counter);

    // --- Divergence Status & Queries ---
    [[nodiscard]] bool has_diverged() const noexcept { return divergence_.has_diverged(); }
    [[nodiscard]] const ReplayDivergenceInfo& divergence() const noexcept { return divergence_; }

    void record_divergence(ReplayDivergenceCategory category,
                           std::uint16_t core_id, std::uint64_t pc,
                           std::uint64_t ret_count, std::uint16_t sub_ord,
                           sndl::SndlBoundaryType exp_bnd, sndl::SndlBoundaryType act_bnd,
                           sndl::SndlEventType exp_ev, sndl::SndlEventType act_ev,
                           std::uint64_t exp_val, std::uint64_t act_val,
                           std::string message);

private:
    ReplayEventCursor& cursor_;
    std::string session_id_;
    ReplayDivergenceInfo divergence_{};
    ReplayProfile* profiler_{nullptr};
};

// -----------------------------------------------------------------------------
// Global Active Replay Router API
// -----------------------------------------------------------------------------
void set_active_replay_router(ReplayInputRouter* router) noexcept;
[[nodiscard]] ReplayInputRouter* get_active_replay_router() noexcept;
[[nodiscard]] bool is_replay_active() noexcept;

// High-performance inline hooks called from CPU and MemoryAccessor
inline bool hook_replay_mmio_read(std::uint16_t core_id, std::uint64_t pc,
                                  std::uint64_t ret_count, std::uint64_t paddr,
                                  unsigned width, std::uint64_t& out_value) {
    if (auto* r = get_active_replay_router()) {
        return r->consume_mmio_read(core_id, pc, ret_count, paddr, width, out_value);
    }
    return false;
}

inline bool hook_replay_counter_read(std::uint16_t core_id, std::uint64_t pc,
                                     std::uint64_t ret_count, std::uint32_t reg_key,
                                     std::uint64_t& out_value) {
    if (auto* r = get_active_replay_router()) {
        return r->consume_counter_read(core_id, pc, ret_count, reg_key, out_value);
    }
    return false;
}

inline bool hook_replay_entropy(std::uint16_t core_id, std::uint64_t pc,
                                std::uint64_t ret_count, unsigned width,
                                std::uint64_t& out_value) {
    if (auto* r = get_active_replay_router()) {
        return r->consume_entropy(core_id, pc, ret_count, width, out_value);
    }
    return false;
}

} // namespace winios::replay
