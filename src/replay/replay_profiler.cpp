#include "replay/replay_profiler.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::replay {

namespace {

std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

void append_phase_json(std::ostringstream& ss, const char* name, const ReplayProfilePhase& phase) {
    ss << "    \"" << name << "\": {\"calls\": " << phase.calls
       << ", \"total_ms\": " << std::fixed << std::setprecision(3) << phase.total_ms
       << ", \"average_ms\": " << std::fixed << std::setprecision(6) << phase.average_ms()
       << ", \"min_ms\": " << std::fixed << std::setprecision(6) << phase.min_ms
       << ", \"max_ms\": " << std::fixed << std::setprecision(6) << phase.max_ms
       << "}";
}

void append_phase_text(std::ostringstream& ss, const char* name, const ReplayProfilePhase& phase) {
    ss << "  " << std::left << std::setw(26) << name
       << std::right << std::setw(10) << phase.calls
       << std::setw(14) << std::fixed << std::setprecision(3) << phase.total_ms
       << std::setw(14) << std::fixed << std::setprecision(4) << phase.average_ms()
       << "\n";
}

double percent_of(double value, double total) {
    return total > 0.0 ? (value * 100.0 / total) : 0.0;
}

} // namespace

std::string ReplayProfiler::to_text(const ReplayProfile& p) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                    WINIOS STAGE 21 REPLAY PROFILE                              \n";
    ss << "================================================================================\n";
    ss << "Profile Label:          " << p.label << "\n";
    ss << "Profiling Enabled:      " << (p.enabled ? "yes" : "no") << "\n";
    ss << "Completion Status:      " << replay_completion_status_name(p.completion_status) << "\n";
    ss << "Diverged:               " << (p.diverged ? "yes" : "no") << "\n";
    ss << "Instructions Executed:  " << p.instructions_executed << "\n";
    ss << "Events Consumed:        " << p.events_consumed << "\n";
    ss << "Checkpoints Recorded:   " << p.checkpoints_recorded << "\n";
    ss << "WSNap File Size:        " << p.wsnap_file_size << " bytes\n";
    ss << "SNDL File Size:         " << p.sndl_file_size << " bytes\n";
    ss << "Guest Memory Capacity:  " << p.memory_capacity << " bytes\n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << "  PHASE                          CALLS      TOTAL_MS    AVG_MS\n";
    ss << "--------------------------------------------------------------------------------\n";
    append_phase_text(ss, "prepare_total", p.prepare_total);
    append_phase_text(ss, "snapshot_validate", p.snapshot_validate);
    append_phase_text(ss, "sndl_open", p.sndl_open);
    append_phase_text(ss, "snapshot_inspect", p.snapshot_inspect);
    append_phase_text(ss, "environment_init", p.environment_init);
    append_phase_text(ss, "snapshot_restore", p.snapshot_restore);
    append_phase_text(ss, "routing_init", p.routing_init);
    append_phase_text(ss, "run_total", p.run_total);
    append_phase_text(ss, "interrupt_injection", p.interrupt_injection);
    append_phase_text(ss, "cpu_execution", p.cpu_execution);
    append_phase_text(ss, "  input_routing", p.input_routing);
    append_phase_text(ss, "checkpoint_total", p.checkpoint_total);
    append_phase_text(ss, "  checkpoint_hash", p.checkpoint_hash);
    append_phase_text(ss, "    memory_hash", p.memory_hash);
    append_phase_text(ss, "final_hash", p.final_hash);
    append_phase_text(ss, "completion_validation", p.completion_validation);
    append_phase_text(ss, "report_generation", p.report_generation);
    append_phase_text(ss, "cursor_block_load", p.cursor_block_load);
    append_phase_text(ss, "  cursor_block_crc", p.cursor_block_crc);
    append_phase_text(ss, "  cursor_block_decode", p.cursor_block_decode);
    ss << "--------------------------------------------------------------------------------\n";
    ss << "Deterministic Counters\n";
    ss << "  Cursor Advances:        " << p.cursor_advances << "\n";
    ss << "  Blocks Loaded:          " << p.blocks_loaded << "\n";
    ss << "  Block Bytes Read:       " << p.block_bytes_read << "\n";
    ss << "  Records Decoded:        " << p.records_decoded << "\n";
    ss << "  CRC Calls / Bytes:      " << p.crc_calls << " / " << p.crc_bytes << "\n";
    ss << "  Hash Calls:             " << p.hash_calls << "\n";
    ss << "  Memory Hash Calls/Bytes:" << p.memory_hash_calls << " / " << p.memory_bytes_hashed << "\n";
    ss << "  Input Route Attempts:   " << p.input_route_attempts << "\n";
    ss << "  Input Routes Consumed:  " << p.input_routes_consumed << "\n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << "Run Accounting\n";
    ss << "  Run Wall Time:          " << std::fixed << std::setprecision(3) << p.run_total.total_ms << " ms\n";
    ss << "  Accounted Phases:       " << p.accounted_execution_ms() << " ms ("
       << std::fixed << std::setprecision(2) << percent_of(p.accounted_execution_ms(), p.run_total.total_ms) << "%)\n";
    ss << "  Unaccounted / Loop:     " << p.unaccounted_execution_ms() << " ms ("
       << std::fixed << std::setprecision(2) << percent_of(p.unaccounted_execution_ms(), p.run_total.total_ms) << "%)\n";
    ss << "  Guest MIPS (run wall):  ";
    if (p.run_total.total_ms > 0.0) {
        ss << std::fixed << std::setprecision(3)
           << (static_cast<double>(p.instructions_executed) / (p.run_total.total_ms / 1000.0)) / 1e6;
    } else {
        ss << "n/a";
    }
    ss << "\n";
    ss << "================================================================================\n";
    return ss.str();
}

std::string ReplayProfiler::to_json(const ReplayProfile& p) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"label\": \"" << escape_json(p.label) << "\",\n";
    ss << "  \"enabled\": " << (p.enabled ? "true" : "false") << ",\n";
    ss << "  \"completion_status\": \"" << replay_completion_status_name(p.completion_status) << "\",\n";
    ss << "  \"diverged\": " << (p.diverged ? "true" : "false") << ",\n";
    ss << "  \"counters\": {\n";
    ss << "    \"instructions_executed\": " << p.instructions_executed << ",\n";
    ss << "    \"events_consumed\": " << p.events_consumed << ",\n";
    ss << "    \"checkpoints_recorded\": " << p.checkpoints_recorded << ",\n";
    ss << "    \"cursor_advances\": " << p.cursor_advances << ",\n";
    ss << "    \"blocks_loaded\": " << p.blocks_loaded << ",\n";
    ss << "    \"block_bytes_read\": " << p.block_bytes_read << ",\n";
    ss << "    \"records_decoded\": " << p.records_decoded << ",\n";
    ss << "    \"crc_calls\": " << p.crc_calls << ",\n";
    ss << "    \"crc_bytes\": " << p.crc_bytes << ",\n";
    ss << "    \"hash_calls\": " << p.hash_calls << ",\n";
    ss << "    \"memory_hash_calls\": " << p.memory_hash_calls << ",\n";
    ss << "    \"memory_bytes_hashed\": " << p.memory_bytes_hashed << ",\n";
    ss << "    \"input_route_attempts\": " << p.input_route_attempts << ",\n";
    ss << "    \"input_routes_consumed\": " << p.input_routes_consumed << "\n";
    ss << "  },\n";
    ss << "  \"artifacts\": {\n";
    ss << "    \"wsnap_file_size\": " << p.wsnap_file_size << ",\n";
    ss << "    \"sndl_file_size\": " << p.sndl_file_size << ",\n";
    ss << "    \"memory_capacity\": " << p.memory_capacity << "\n";
    ss << "  },\n";
    ss << "  \"phases\": {\n";
    append_phase_json(ss, "prepare_total", p.prepare_total); ss << ",\n";
    append_phase_json(ss, "snapshot_validate", p.snapshot_validate); ss << ",\n";
    append_phase_json(ss, "sndl_open", p.sndl_open); ss << ",\n";
    append_phase_json(ss, "snapshot_inspect", p.snapshot_inspect); ss << ",\n";
    append_phase_json(ss, "environment_init", p.environment_init); ss << ",\n";
    append_phase_json(ss, "snapshot_restore", p.snapshot_restore); ss << ",\n";
    append_phase_json(ss, "routing_init", p.routing_init); ss << ",\n";
    append_phase_json(ss, "run_total", p.run_total); ss << ",\n";
    append_phase_json(ss, "interrupt_injection", p.interrupt_injection); ss << ",\n";
    append_phase_json(ss, "cpu_execution", p.cpu_execution); ss << ",\n";
    append_phase_json(ss, "input_routing", p.input_routing); ss << ",\n";
    append_phase_json(ss, "checkpoint_total", p.checkpoint_total); ss << ",\n";
    append_phase_json(ss, "checkpoint_hash", p.checkpoint_hash); ss << ",\n";
    append_phase_json(ss, "memory_hash", p.memory_hash); ss << ",\n";
    append_phase_json(ss, "final_hash", p.final_hash); ss << ",\n";
    append_phase_json(ss, "completion_validation", p.completion_validation); ss << ",\n";
    append_phase_json(ss, "report_generation", p.report_generation); ss << ",\n";
    append_phase_json(ss, "cursor_block_load", p.cursor_block_load); ss << ",\n";
    append_phase_json(ss, "cursor_block_crc", p.cursor_block_crc); ss << ",\n";
    append_phase_json(ss, "cursor_block_decode", p.cursor_block_decode); ss << "\n";
    ss << "  },\n";
    ss << "  \"accounting\": {\n";
    ss << "    \"accounted_ms\": " << std::fixed << std::setprecision(3) << p.accounted_execution_ms() << ",\n";
    ss << "    \"unaccounted_ms\": " << std::fixed << std::setprecision(3) << p.unaccounted_execution_ms() << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

ReplayResult ReplayProfiler::write_text(
    const std::filesystem::path& path,
    const ReplayProfile& profile
) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to open profile text output: " + path.string());
    }
    out << to_text(profile);
    if (!out.good()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to write profile text output: " + path.string());
    }
    return ReplayResult::ok();
}

ReplayResult ReplayProfiler::write_json(
    const std::filesystem::path& path,
    const ReplayProfile& profile
) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to open profile JSON output: " + path.string());
    }
    out << to_json(profile);
    if (!out.good()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to write profile JSON output: " + path.string());
    }
    return ReplayResult::ok();
}

} // namespace winios::replay
