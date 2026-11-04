#include "replay/replay_report.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::replay {

namespace {

std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                std::ostringstream ss;
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                out += ss.str();
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

std::string to_hex_u64(std::uint64_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << val;
    return ss.str();
}

} // namespace

std::string ReplayReport::to_json(
    const ReplayExecutionStats& stats,
    const std::string& session_id
) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"session_id\": \"" << escape_json(session_id) << "\",\n";
    ss << "  \"diverged\": " << (stats.diverged ? "true" : "false") << ",\n";
    ss << "  \"completion_status\": \"" << replay_completion_status_name(stats.completion_status) << "\",\n";
    ss << "  \"execution_stats\": {\n";
    ss << "    \"instructions_executed\": " << stats.instructions_executed << ",\n";
    ss << "    \"events_consumed\": " << stats.events_consumed << ",\n";
    ss << "    \"checkpoints_taken\": " << stats.checkpoints_taken << ",\n";
    ss << "    \"elapsed_ms\": " << std::fixed << std::setprecision(3) << stats.elapsed_ms << "\n";
    ss << "  },\n";

    const auto& div = stats.divergence_info;
    ss << "  \"divergence\": {\n";
    ss << "    \"category\": \"" << replay_divergence_category_name(div.category) << "\",\n";
    ss << "    \"category_id\": " << static_cast<std::uint32_t>(div.category) << ",\n";
    ss << "    \"sequence_num\": " << div.sequence_num << ",\n";
    ss << "    \"core_id\": " << div.core_id << ",\n";
    ss << "    \"pc\": \"" << to_hex_u64(div.pc) << "\",\n";
    ss << "    \"retirement_count\": " << div.retirement_count << ",\n";
    ss << "    \"sub_instruction_ordinal\": " << div.sub_instruction_ordinal << ",\n";
    ss << "    \"expected_boundary\": \"" << sndl::sndl_boundary_type_name(div.expected_boundary) << "\",\n";
    ss << "    \"actual_boundary\": \"" << sndl::sndl_boundary_type_name(div.actual_boundary) << "\",\n";
    ss << "    \"expected_event_type\": \"" << sndl::sndl_event_type_name(div.expected_event_type) << "\",\n";
    ss << "    \"actual_event_type\": \"" << sndl::sndl_event_type_name(div.actual_event_type) << "\",\n";
    ss << "    \"expected_value\": \"" << to_hex_u64(div.expected_value) << "\",\n";
    ss << "    \"actual_value\": \"" << to_hex_u64(div.actual_value) << "\",\n";
    ss << "    \"message\": \"" << escape_json(div.message) << "\"\n";
    ss << "  },\n";

    const auto& act_h = stats.diverged ? div.actual_hashes : stats.final_state_hashes;
    const auto& exp_h = div.expected_hashes;

    ss << "  \"expected_hashes\": {\n";
    ss << "    \"cpu_registers\": \"" << to_hex_u64(exp_h.cpu_registers_hash) << "\",\n";
    ss << "    \"simd_registers\": \"" << to_hex_u64(exp_h.simd_registers_hash) << "\",\n";
    ss << "    \"system_registers\": \"" << to_hex_u64(exp_h.system_registers_hash) << "\",\n";
    ss << "    \"memory\": \"" << to_hex_u64(exp_h.memory_hash) << "\",\n";
    ss << "    \"timer\": \"" << to_hex_u64(exp_h.timer_hash) << "\",\n";
    ss << "    \"gic\": \"" << to_hex_u64(exp_h.gic_hash) << "\",\n";
    ss << "    \"combined\": \"" << to_hex_u64(exp_h.combined_hash) << "\"\n";
    ss << "  },\n";

    ss << "  \"actual_hashes\": {\n";
    ss << "    \"cpu_registers\": \"" << to_hex_u64(act_h.cpu_registers_hash) << "\",\n";
    ss << "    \"simd_registers\": \"" << to_hex_u64(act_h.simd_registers_hash) << "\",\n";
    ss << "    \"system_registers\": \"" << to_hex_u64(act_h.system_registers_hash) << "\",\n";
    ss << "    \"memory\": \"" << to_hex_u64(act_h.memory_hash) << "\",\n";
    ss << "    \"timer\": \"" << to_hex_u64(act_h.timer_hash) << "\",\n";
    ss << "    \"gic\": \"" << to_hex_u64(act_h.gic_hash) << "\",\n";
    ss << "    \"combined\": \"" << to_hex_u64(act_h.combined_hash) << "\"\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

ReplayResult ReplayReport::write_json(
    const std::filesystem::path& path,
    const ReplayExecutionStats& stats,
    const std::string& session_id
) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError, "Failed to open report output path: " + path.string());
    }

    out << to_json(stats, session_id);
    if (!out.good()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError, "Failed to write report json to: " + path.string());
    }

    return ReplayResult::ok();
}

std::string ReplayReport::to_summary_string(
    const ReplayExecutionStats& stats
) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                       WINIOS DETERMINISTIC REPLAY REPORT                       \n";
    ss << "================================================================================\n";
    ss << "Status:                 " << (stats.diverged ? "DIVERGED" : "MATCH / SUCCESS")
       << " [" << replay_completion_status_name(stats.completion_status) << "]\n";
    ss << "Instructions Executed:  " << stats.instructions_executed << "\n";
    ss << "Events Consumed:        " << stats.events_consumed << "\n";
    ss << "Checkpoints Taken:      " << stats.checkpoints_taken << "\n";
    ss << "Elapsed Time:           " << std::fixed << std::setprecision(2) << stats.elapsed_ms << " ms\n";
    if (stats.elapsed_ms > 0.0) {
        const double mips = (static_cast<double>(stats.instructions_executed) / (stats.elapsed_ms / 1000.0)) / 1000000.0;
        ss << "Execution Rate:         " << std::fixed << std::setprecision(2) << mips << " MIPS\n";
    }

    if (stats.diverged) {
        const auto& div = stats.divergence_info;
        ss << "--------------------------------------------------------------------------------\n";
        ss << "                             DIVERGENCE DIAGNOSTICS                             \n";
        ss << "--------------------------------------------------------------------------------\n";
        ss << "Category:               " << replay_divergence_category_name(div.category)
           << " (Code " << static_cast<std::uint32_t>(div.category) << ")\n";
        ss << "Sequence Number:        " << div.sequence_num << "\n";
        ss << "Core ID:                " << div.core_id << "\n";
        ss << "Program Counter:        " << to_hex_u64(div.pc) << "\n";
        ss << "Retirement Count:       " << div.retirement_count << "\n";
        ss << "Sub-instruction Ord:    " << div.sub_instruction_ordinal << "\n";
        ss << "Expected Boundary:      " << sndl::sndl_boundary_type_name(div.expected_boundary) << "\n";
        ss << "Actual Boundary:        " << sndl::sndl_boundary_type_name(div.actual_boundary) << "\n";
        ss << "Expected Event Type:    " << sndl::sndl_event_type_name(div.expected_event_type) << "\n";
        ss << "Actual Event Type:      " << sndl::sndl_event_type_name(div.actual_event_type) << "\n";
        ss << "Expected Value:         " << to_hex_u64(div.expected_value) << "\n";
        ss << "Actual Value:           " << to_hex_u64(div.actual_value) << "\n";
        ss << "Diagnostics Message:    " << div.message << "\n";
    }

    ss << "--------------------------------------------------------------------------------\n";
    ss << "                             STATE HASH SUMMARY                                 \n";
    ss << "--------------------------------------------------------------------------------\n";
    const auto& h = stats.final_state_hashes;
    ss << "CPU Registers Hash:     " << to_hex_u64(h.cpu_registers_hash) << "\n";
    ss << "SIMD Registers Hash:    " << to_hex_u64(h.simd_registers_hash) << "\n";
    ss << "System Registers Hash:  " << to_hex_u64(h.system_registers_hash) << "\n";
    ss << "Guest Memory Hash:      " << to_hex_u64(h.memory_hash) << "\n";
    ss << "Timer State Hash:       " << to_hex_u64(h.timer_hash) << "\n";
    ss << "GICv3 State Hash:       " << to_hex_u64(h.gic_hash) << "\n";
    ss << "Combined State Hash:    " << to_hex_u64(h.combined_hash) << "\n";
    ss << "================================================================================\n";

    return ss.str();
}

} // namespace winios::replay
