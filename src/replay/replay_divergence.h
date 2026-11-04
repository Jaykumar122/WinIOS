#pragma once

#include "replay/replay_state_hash.h"
#include "replay/replay_types.h"
#include "sndl/sndl_types.h"
#include "trace/trace_event.h"

#include <cstdint>
#include <string>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayDivergenceInfo: Complete First-Divergence Diagnostics
// -----------------------------------------------------------------------------
struct ReplayDivergenceInfo {
    ReplayDivergenceCategory category{ReplayDivergenceCategory::None};
    std::string session_id;
    std::uint64_t sequence_num{0};
    std::uint16_t core_id{0};
    std::uint64_t pc{0};
    std::uint64_t retirement_count{0};
    std::uint16_t sub_instruction_ordinal{0};
    sndl::SndlBoundaryType expected_boundary{sndl::SndlBoundaryType::Unknown};
    sndl::SndlBoundaryType actual_boundary{sndl::SndlBoundaryType::Unknown};
    sndl::SndlEventType expected_event_type{sndl::SndlEventType::Unknown};
    sndl::SndlEventType actual_event_type{sndl::SndlEventType::Unknown};
    std::uint64_t expected_value{0};
    std::uint64_t actual_value{0};
    std::string message;

    ReplayComponentHashes expected_hashes{};
    ReplayComponentHashes actual_hashes{};
    std::vector<trace::TraceEvent> recent_trace_events{};

    [[nodiscard]] bool has_diverged() const noexcept {
        return category != ReplayDivergenceCategory::None;
    }

    void reset() noexcept {
        category = ReplayDivergenceCategory::None;
        session_id.clear();
        sequence_num = 0;
        core_id = 0;
        pc = 0;
        retirement_count = 0;
        sub_instruction_ordinal = 0;
        expected_boundary = sndl::SndlBoundaryType::Unknown;
        actual_boundary = sndl::SndlBoundaryType::Unknown;
        expected_event_type = sndl::SndlEventType::Unknown;
        actual_event_type = sndl::SndlEventType::Unknown;
        expected_value = 0;
        actual_value = 0;
        message.clear();
        expected_hashes = {};
        actual_hashes = {};
        recent_trace_events.clear();
    }
};

} // namespace winios::replay
