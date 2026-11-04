#include "replay/replay_divergence.h"
#include <iomanip>
#include <sstream>

namespace winios::replay {

// Diagnostic helper string formatter if needed
std::string format_divergence_summary(const ReplayDivergenceInfo& div) {
    if (!div.has_diverged()) {
        return "No divergence detected.";
    }

    std::ostringstream ss;
    ss << "=== REPLAY DIVERGENCE DETECTED ===\n";
    ss << "  Category:               " << replay_divergence_category_name(div.category) << "\n";
    ss << "  Session ID:             " << div.session_id << "\n";
    ss << "  SNDL Sequence Number:   " << div.sequence_num << "\n";
    ss << "  Core ID:                " << div.core_id << "\n";
    ss << "  Program Counter:        0x" << std::hex << div.pc << std::dec << "\n";
    ss << "  Retirement Count:       " << div.retirement_count << "\n";
    ss << "  Sub-Instruction Ord:    " << div.sub_instruction_ordinal << "\n";
    ss << "  Expected Boundary:      " << sndl::sndl_boundary_type_name(div.expected_boundary) << "\n";
    ss << "  Actual Boundary:        " << sndl::sndl_boundary_type_name(div.actual_boundary) << "\n";
    ss << "  Expected Event:         " << sndl::sndl_event_type_name(div.expected_event_type) << "\n";
    ss << "  Actual Event:           " << sndl::sndl_event_type_name(div.actual_event_type) << "\n";
    if (div.expected_value != 0 || div.actual_value != 0) {
        ss << "  Expected Value:         0x" << std::hex << div.expected_value << std::dec << "\n";
        ss << "  Actual Value:           0x" << std::hex << div.actual_value << std::dec << "\n";
    }
    ss << "  Diagnostic Message:     " << div.message << "\n";

    return ss.str();
}

} // namespace winios::replay
