#pragma once

#include "cpu/smp_core_state.h"

#include <cstddef>
#include <cstdint>

namespace winios::replay {

// -----------------------------------------------------------------------------
// SmpReplayCoreState: Replay-Specific Per-Core Tracking Structure
// -----------------------------------------------------------------------------
struct SmpReplayCoreState {
    std::size_t core_id{0};
    std::uint64_t total_instructions_retired{0};
    std::uint64_t quantums_allocated{0};
    std::uint64_t early_truncations{0};
    std::uint64_t active_quantum_budget{0};
    std::uint64_t active_quantum_executed{0};
    CoreLifecycleState lifecycle{CoreLifecycleState::Runnable};
    bool active{true};
    bool is_eligible{true};

    void reset(std::size_t id) noexcept {
        core_id = id;
        total_instructions_retired = 0;
        quantums_allocated = 0;
        early_truncations = 0;
        active_quantum_budget = 0;
        active_quantum_executed = 0;
        lifecycle = CoreLifecycleState::Runnable;
        active = true;
        is_eligible = true;
    }
};

} // namespace winios::replay
