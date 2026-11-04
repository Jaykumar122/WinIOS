#include "trace_analysis_report.h"

#include <iomanip>
#include <sstream>

namespace winios::trace {

std::string escape_json_string(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string format_inspection_report(const TraceFileInspectionResult& res) {
    std::ostringstream ss;
    ss << "=== WinIOS Trace File Inspection ===\n";
    ss << "File Path:         " << res.file_path << "\n";
    ss << "File Size:         " << res.file_size << " bytes ("
       << std::fixed << std::setprecision(2) << (static_cast<double>(res.file_size) / 1024.0) << " KiB)\n";
    ss << "Format Version:    " << res.format_version << " (Schema: 0x" << std::hex << res.schema_version << std::dec << ")\n";
    ss << "Integrity Status:  " << to_string(res.integrity_status) << "\n";
    ss << "Header Valid:      " << (res.header_valid ? "YES" : "NO") << "\n";
    ss << "Footer Valid:      " << (res.footer_valid ? "YES" : "NO") << "\n";
    ss << "CRC32C Valid:      " << (res.crc_valid ? "YES" : "MISMATCH") << "\n";
    ss << "Recorded Blocks:   " << res.block_count << "\n";
    ss << "Recorded Events:   " << res.event_count << "\n";
    if (res.footer_valid) {
        ss << "Footer Total Ev:   " << res.footer_expected_events << "\n";
        ss << "Footer Total Blk:  " << res.footer_expected_blocks << "\n";
    }
    ss << "Dropped Markers:   " << res.dropped_markers_count << "\n";
    ss << "Unknown Events:    " << res.unknown_event_types_count << "\n";
    if (res.event_count > 0) {
        ss << "Timestamp Range:   " << res.trace_start_time << " -> " << res.trace_end_time
           << " (duration: " << (res.trace_end_time - res.trace_start_time) << " ticks)\n";
    }

    if (!res.per_core_event_counts.empty()) {
        ss << "\n--- Per-Core Event Breakdown ---\n";
        for (const auto& pair : res.per_core_event_counts) {
            ss << "  Core " << pair.first << ": " << pair.second << " events\n";
        }
    }

    if (!res.diagnostics.empty()) {
        ss << "\n--- Inspection Diagnostics (" << res.diagnostics.size() << ") ---\n";
        for (const auto& d : res.diagnostics) {
            ss << "  [" << to_string(d.severity) << "] "
               << to_string(d.code) << " at offset 0x" << std::hex << d.file_offset << std::dec;
            if (d.core_id > 0) ss << " (Core " << d.core_id << ")";
            ss << ": " << d.message << "\n";
        }
    }
    return ss.str();
}

std::string format_summary_report(const TraceAnalysisReport& rep) {
    std::ostringstream ss;
    ss << format_inspection_report(rep.inspection);

    ss << "\n=== WinIOS Trace Execution Analysis Summary ===\n";
    ss << "Filtered Events:   " << rep.filtered_events_count << " / " << rep.inspection.event_count << "\n\n";

    // Instructions
    ss << "--- 1. Instruction Retirement ---\n";
    ss << "  Total Retired:     " << rep.instructions.total_retired << "\n";
    if (rep.instructions.total_retired > 0) {
        ss << "  PC Range:          0x" << std::hex << rep.instructions.min_pc
           << " -> 0x" << rep.instructions.max_pc << std::dec << "\n";
        ss << "  First/Last PC:     0x" << std::hex << rep.instructions.first_pc
           << " / 0x" << rep.instructions.last_pc << std::dec << "\n";
        ss << "  Unique PCs:        " << rep.instructions.unique_pc_count << "\n";
    }
    ss << "  Sequence Gaps:     " << rep.instructions.sequence_gaps << "\n";
    ss << "  Regressions:       " << rep.instructions.sequence_regressions << "\n";

    // Branches
    ss << "\n--- 2. Branch Control Flow ---\n";
    ss << "  Total Taken:       " << rep.branches.total_branches << "\n";
    ss << "  Forward Branches:  " << rep.branches.forward_branches << "\n";
    ss << "  Backward Loops:    " << rep.branches.backward_branches << "\n";
    ss << "  Missing Target:    " << rep.branches.branches_without_target << "\n";

    // Exceptions
    ss << "\n--- 3. Exception & Interrupt Architecture ---\n";
    ss << "  Exception Entries: " << rep.exceptions.total_entries
       << " (Sync: " << rep.exceptions.synchronous_entries
       << ", Async: " << rep.exceptions.asynchronous_entries << ")\n";
    ss << "  Exception Returns: " << rep.exceptions.total_returns << "\n";
    ss << "  Entries by EL:     EL0=" << rep.exceptions.entries_per_el[0]
       << ", EL1=" << rep.exceptions.entries_per_el[1]
       << ", EL2=" << rep.exceptions.entries_per_el[2]
       << ", EL3=" << rep.exceptions.entries_per_el[3] << "\n";
    ss << "  Unmatched Returns: " << rep.exceptions.unmatched_returns << "\n";
    ss << "  Unmatched Entries: " << rep.exceptions.unmatched_entries << "\n";

    // Memory
    ss << "\n--- 4. Memory Subsystem ---\n";
    ss << "  Total Reads:       " << rep.memory.total_reads << "\n";
    ss << "  Total Writes:      " << rep.memory.total_writes << "\n";
    ss << "  Atomic Ops:        " << rep.memory.total_atomics << "\n";
    if (rep.memory.total_reads + rep.memory.total_writes > 0) {
        ss << "  Address Range:     0x" << std::hex << rep.memory.min_address
           << " -> 0x" << rep.memory.max_address << std::dec << "\n";
    }
    ss << "  Sizes (B):         1B=" << rep.memory.size_1b
       << ", 2B=" << rep.memory.size_2b
       << ", 4B=" << rep.memory.size_4b
       << ", 8B=" << rep.memory.size_8b
       << ", 16B=" << rep.memory.size_16b
       << ", other=" << rep.memory.size_other << "\n";

    // System Registers
    ss << "\n--- 5. System Register Accesses ---\n";
    ss << "  Total Accesses:    " << rep.sysregs.total_accesses
       << " (Reads: " << rep.sysregs.total_reads
       << ", Writes: " << rep.sysregs.total_writes << ")\n";
    ss << "  Unique Registers:  " << rep.sysregs.register_counts.size() << "\n";

    // Timers
    ss << "\n--- 6. Timer Subsystem ---\n";
    ss << "  Compare Updates:   " << rep.timers.compare_updates << "\n";
    ss << "  Control Updates:   " << rep.timers.control_updates << "\n";
    ss << "  Expirations:       " << rep.timers.expirations << "\n";
    ss << "  Assertions (IRQ):  " << rep.timers.interrupt_assertions << "\n";
    ss << "  Deassertions:      " << rep.timers.interrupt_clears << "\n";
    ss << "  Resets:            " << rep.timers.resets << "\n";
    ss << "  By Type:           Phys=" << rep.timers.per_type_counts[0]
       << ", Virt=" << rep.timers.per_type_counts[1]
       << ", Hyp=" << rep.timers.per_type_counts[2] << "\n";

    // Causal Chains
    if (!rep.causal_chains.empty()) {
        ss << "\n--- 7. Timer & Interrupt Causal Chains (" << rep.causal_chains.size() << ") ---\n";
        for (std::size_t i = 0; i < rep.causal_chains.size(); ++i) {
            const auto& c = rep.causal_chains[i];
            ss << "  Chain #" << (i + 1) << " [Core " << c.core_id << "] "
               << to_string(c.status) << ": " << c.explanation << "\n";
            ss << "    ExpireSeq=" << c.expire_seq << " (t=" << c.expire_time << ")"
               << " -> AssertSeq=" << c.assert_seq << " (t=" << c.assert_time << ")"
               << " -> DeliverSeq=" << c.deliver_seq << " (t=" << c.deliver_time << ", INTID=" << c.intid << ")"
               << " -> EntrySeq=" << c.entry_seq << " (t=" << c.entry_time << ")"
               << " -> ContextChangeSeq=" << c.context_change_seq << " (t=" << c.context_change_time << ")\n";
        }
    }

    // Sequence Diagnostics
    if (!rep.sequence_diagnostics.empty()) {
        ss << "\n--- 8. Sequence Validation Diagnostics (" << rep.sequence_diagnostics.size() << ") ---\n";
        for (const auto& d : rep.sequence_diagnostics) {
            ss << "  [" << to_string(d.severity) << "] Core " << d.core_id
               << " Seq " << d.sequence_number << " (" << to_string(d.code) << "): "
               << d.message << "\n";
        }
    }
    return ss.str();
}

std::string format_validation_report(const TraceAnalysisReport& rep) {
    std::ostringstream ss;
    ss << "=== WinIOS Trace Sequence Validation Report ===\n";
    ss << "File: " << rep.inspection.file_path << "\n";
    ss << "Total Events Inspected: " << rep.inspection.event_count << "\n";

    std::size_t error_count = 0;
    std::size_t warning_count = 0;
    std::size_t info_count = 0;

    for (const auto& d : rep.inspection.diagnostics) {
        if (d.severity == DiagnosticSeverity::Error) error_count++;
        else if (d.severity == DiagnosticSeverity::Warning) warning_count++;
        else info_count++;
    }
    for (const auto& d : rep.sequence_diagnostics) {
        if (d.severity == DiagnosticSeverity::Error) error_count++;
        else if (d.severity == DiagnosticSeverity::Warning) warning_count++;
        else info_count++;
    }

    ss << "Validation Status: " << (error_count == 0 ? "PASSED" : "FAILED") << "\n";
    ss << "Diagnostics Summary: Errors=" << error_count
       << ", Warnings=" << warning_count << ", Infos=" << info_count << "\n\n";

    if (!rep.inspection.diagnostics.empty()) {
        ss << "--- File Integrity Diagnostics ---\n";
        for (const auto& d : rep.inspection.diagnostics) {
            ss << "  [" << to_string(d.severity) << "] "
               << to_string(d.code) << ": " << d.message << "\n";
        }
        ss << "\n";
    }

    if (!rep.sequence_diagnostics.empty()) {
        ss << "--- Architectural Sequence Diagnostics ---\n";
        for (const auto& d : rep.sequence_diagnostics) {
            ss << "  [" << to_string(d.severity) << "] Core " << d.core_id
               << " Seq " << d.sequence_number << ": " << d.message
               << " (DefinitelyInvalid: " << (d.is_definitely_invalid ? "YES" : "NO") << ")\n";
        }
    } else {
        ss << "No architectural sequence violations or anomalies detected.\n";
    }
    return ss.str();
}

std::string format_timeline_report(const std::vector<TimelineEntry>& timeline) {
    std::ostringstream ss;
    ss << "=== WinIOS Execution Timeline (" << timeline.size() << " events) ===\n";
    ss << std::left
       << std::setw(8)  << "Idx"
       << std::setw(8)  << "Seq"
       << std::setw(6)  << "Core"
       << std::setw(10) << "Time"
       << std::setw(6)  << "EL"
       << std::setw(22) << "EventType"
       << "Details\n";
    ss << std::string(80, '-') << "\n";

    for (const auto& te : timeline) {
        ss << std::left
           << std::setw(8)  << te.file_index
           << std::setw(8)  << te.sequence_number
           << std::setw(6)  << te.core_id
           << std::setw(10) << te.timestamp
           << "EL" << std::setw(4) << static_cast<unsigned>(te.exception_level)
           << std::setw(22) << to_string(te.event_type)
           << te.decoded_info << "\n";
    }
    return ss.str();
}

std::string export_timeline_to_json(const std::vector<TimelineEntry>& timeline) {
    std::ostringstream ss;
    ss << "[\n";
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        const auto& t = timeline[i];
        ss << "  {\n";
        ss << "    \"file_index\": " << t.file_index << ",\n";
        ss << "    \"sequence_number\": " << t.sequence_number << ",\n";
        ss << "    \"core_id\": " << t.core_id << ",\n";
        ss << "    \"timestamp\": " << t.timestamp << ",\n";
        ss << "    \"event_type\": \"" << to_string(t.event_type) << "\",\n";
        ss << "    \"exception_level\": " << static_cast<unsigned>(t.exception_level) << ",\n";
        ss << "    \"pc\": \"0x" << std::hex << t.pc << std::dec << "\",\n";
        ss << "    \"address\": \"0x" << std::hex << t.address << std::dec << "\",\n";
        ss << "    \"data\": \"0x" << std::hex << t.data << std::dec << "\",\n";
        ss << "    \"opcode_or_syndrome\": \"0x" << std::hex << t.opcode_or_syndrome << std::dec << "\",\n";
        ss << "    \"flags\": \"0x" << std::hex << t.flags << std::dec << "\",\n";
        ss << "    \"details\": \"" << escape_json_string(t.decoded_info) << "\"\n";
        ss << "  }" << (i + 1 < timeline.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

std::string export_report_to_json(const TraceAnalysisReport& rep) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"trace_file\": \"" << escape_json_string(rep.inspection.file_path) << "\",\n";
    ss << "  \"file_size\": " << rep.inspection.file_size << ",\n";
    ss << "  \"format_version\": " << rep.inspection.format_version << ",\n";
    ss << "  \"schema_version\": \"0x" << std::hex << rep.inspection.schema_version << std::dec << "\",\n";

    // Integrity block
    ss << "  \"integrity\": {\n";
    ss << "    \"status\": \"" << to_string(rep.inspection.integrity_status) << "\",\n";
    ss << "    \"header_valid\": " << (rep.inspection.header_valid ? "true" : "false") << ",\n";
    ss << "    \"footer_valid\": " << (rep.inspection.footer_valid ? "true" : "false") << ",\n";
    ss << "    \"crc_valid\": " << (rep.inspection.crc_valid ? "true" : "false") << ",\n";
    ss << "    \"total_events\": " << rep.inspection.event_count << ",\n";
    ss << "    \"total_blocks\": " << rep.inspection.block_count << ",\n";
    ss << "    \"footer_expected_events\": " << rep.inspection.footer_expected_events << ",\n";
    ss << "    \"footer_expected_blocks\": " << rep.inspection.footer_expected_blocks << ",\n";
    ss << "    \"dropped_markers\": " << rep.inspection.dropped_markers_count << ",\n";
    ss << "    \"unknown_event_types\": " << rep.inspection.unknown_event_types_count << ",\n";
    ss << "    \"start_time\": " << rep.inspection.trace_start_time << ",\n";
    ss << "    \"end_time\": " << rep.inspection.trace_end_time << "\n";
    ss << "  },\n";

    // Subsystems block
    ss << "  \"subsystems\": {\n";

    // Instructions
    ss << "    \"instructions\": {\n";
    ss << "      \"total_retired\": " << rep.instructions.total_retired << ",\n";
    ss << "      \"min_pc\": \"0x" << std::hex << rep.instructions.min_pc << std::dec << "\",\n";
    ss << "      \"max_pc\": \"0x" << std::hex << rep.instructions.max_pc << std::dec << "\",\n";
    ss << "      \"first_pc\": \"0x" << std::hex << rep.instructions.first_pc << std::dec << "\",\n";
    ss << "      \"last_pc\": \"0x" << std::hex << rep.instructions.last_pc << std::dec << "\",\n";
    ss << "      \"unique_pc_count\": " << rep.instructions.unique_pc_count << ",\n";
    ss << "      \"sequence_gaps\": " << rep.instructions.sequence_gaps << ",\n";
    ss << "      \"sequence_regressions\": " << rep.instructions.sequence_regressions << "\n";
    ss << "    },\n";

    // Branches
    ss << "    \"branches\": {\n";
    ss << "      \"total_branches\": " << rep.branches.total_branches << ",\n";
    ss << "      \"forward_branches\": " << rep.branches.forward_branches << ",\n";
    ss << "      \"backward_branches\": " << rep.branches.backward_branches << ",\n";
    ss << "      \"branches_without_target\": " << rep.branches.branches_without_target << "\n";
    ss << "    },\n";

    // Exceptions
    ss << "    \"exceptions\": {\n";
    ss << "      \"total_entries\": " << rep.exceptions.total_entries << ",\n";
    ss << "      \"total_returns\": " << rep.exceptions.total_returns << ",\n";
    ss << "      \"synchronous_entries\": " << rep.exceptions.synchronous_entries << ",\n";
    ss << "      \"asynchronous_entries\": " << rep.exceptions.asynchronous_entries << ",\n";
    ss << "      \"unmatched_entries\": " << rep.exceptions.unmatched_entries << ",\n";
    ss << "      \"unmatched_returns\": " << rep.exceptions.unmatched_returns << ",\n";
    ss << "      \"entries_per_el\": [" << rep.exceptions.entries_per_el[0] << ", "
       << rep.exceptions.entries_per_el[1] << ", " << rep.exceptions.entries_per_el[2] << ", "
       << rep.exceptions.entries_per_el[3] << "],\n";
    ss << "      \"returns_per_el\": [" << rep.exceptions.returns_per_el[0] << ", "
       << rep.exceptions.returns_per_el[1] << ", " << rep.exceptions.returns_per_el[2] << ", "
       << rep.exceptions.returns_per_el[3] << "]\n";
    ss << "    },\n";

    // Memory
    ss << "    \"memory\": {\n";
    ss << "      \"total_reads\": " << rep.memory.total_reads << ",\n";
    ss << "      \"total_writes\": " << rep.memory.total_writes << ",\n";
    ss << "      \"total_atomics\": " << rep.memory.total_atomics << ",\n";
    ss << "      \"min_address\": \"0x" << std::hex << rep.memory.min_address << std::dec << "\",\n";
    ss << "      \"max_address\": \"0x" << std::hex << rep.memory.max_address << std::dec << "\",\n";
    ss << "      \"size_distribution\": {\n";
    ss << "        \"1B\": " << rep.memory.size_1b << ",\n";
    ss << "        \"2B\": " << rep.memory.size_2b << ",\n";
    ss << "        \"4B\": " << rep.memory.size_4b << ",\n";
    ss << "        \"8B\": " << rep.memory.size_8b << ",\n";
    ss << "        \"16B\": " << rep.memory.size_16b << ",\n";
    ss << "        \"other\": " << rep.memory.size_other << "\n";
    ss << "      }\n";
    ss << "    },\n";

    // System Registers
    ss << "    \"system_registers\": {\n";
    ss << "      \"total_accesses\": " << rep.sysregs.total_accesses << ",\n";
    ss << "      \"total_reads\": " << rep.sysregs.total_reads << ",\n";
    ss << "      \"total_writes\": " << rep.sysregs.total_writes << ",\n";
    ss << "      \"unique_registers\": " << rep.sysregs.register_counts.size() << "\n";
    ss << "    },\n";

    // Timers
    ss << "    \"timers\": {\n";
    ss << "      \"compare_updates\": " << rep.timers.compare_updates << ",\n";
    ss << "      \"control_updates\": " << rep.timers.control_updates << ",\n";
    ss << "      \"expirations\": " << rep.timers.expirations << ",\n";
    ss << "      \"interrupt_assertions\": " << rep.timers.interrupt_assertions << ",\n";
    ss << "      \"interrupt_clears\": " << rep.timers.interrupt_clears << ",\n";
    ss << "      \"resets\": " << rep.timers.resets << ",\n";
    ss << "      \"physical_count\": " << rep.timers.per_type_counts[0] << ",\n";
    ss << "      \"virtual_count\": " << rep.timers.per_type_counts[1] << ",\n";
    ss << "      \"hypervisor_count\": " << rep.timers.per_type_counts[2] << "\n";
    ss << "    }\n";
    ss << "  },\n";

    // Per-core array
    ss << "  \"per_core\": [\n";
    for (std::size_t i = 0; i < rep.core_summaries.size(); ++i) {
        const auto& cs = rep.core_summaries[i];
        ss << "    {\n";
        ss << "      \"core_id\": " << cs.core_id << ",\n";
        ss << "      \"event_count\": " << cs.event_count << ",\n";
        ss << "      \"first_sequence\": " << cs.first_sequence << ",\n";
        ss << "      \"last_sequence\": " << cs.last_sequence << ",\n";
        ss << "      \"first_timestamp\": " << cs.first_timestamp << ",\n";
        ss << "      \"last_timestamp\": " << cs.last_timestamp << "\n";
        ss << "    }" << (i + 1 < rep.core_summaries.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // Causal chains
    ss << "  \"causal_chains\": [\n";
    for (std::size_t i = 0; i < rep.causal_chains.size(); ++i) {
        const auto& c = rep.causal_chains[i];
        ss << "    {\n";
        ss << "      \"core_id\": " << c.core_id << ",\n";
        ss << "      \"status\": \"" << to_string(c.status) << "\",\n";
        ss << "      \"timer_type\": " << static_cast<unsigned>(c.timer_type) << ",\n";
        ss << "      \"expire_seq\": " << c.expire_seq << ",\n";
        ss << "      \"expire_time\": " << c.expire_time << ",\n";
        ss << "      \"assert_seq\": " << c.assert_seq << ",\n";
        ss << "      \"assert_time\": " << c.assert_time << ",\n";
        ss << "      \"deliver_seq\": " << c.deliver_seq << ",\n";
        ss << "      \"deliver_time\": " << c.deliver_time << ",\n";
        ss << "      \"intid\": " << c.intid << ",\n";
        ss << "      \"entry_seq\": " << c.entry_seq << ",\n";
        ss << "      \"entry_time\": " << c.entry_time << ",\n";
        ss << "      \"context_change_seq\": " << c.context_change_seq << ",\n";
        ss << "      \"context_change_time\": " << c.context_change_time << ",\n";
        ss << "      \"explanation\": \"" << escape_json_string(c.explanation) << "\"\n";
        ss << "    }" << (i + 1 < rep.causal_chains.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // Diagnostics array
    std::vector<TraceDiagnostic> all_diags = rep.inspection.diagnostics;
    all_diags.insert(all_diags.end(), rep.sequence_diagnostics.begin(), rep.sequence_diagnostics.end());
    ss << "  \"diagnostics\": [\n";
    for (std::size_t i = 0; i < all_diags.size(); ++i) {
        const auto& d = all_diags[i];
        ss << "    {\n";
        ss << "      \"severity\": \"" << to_string(d.severity) << "\",\n";
        ss << "      \"code\": \"" << to_string(d.code) << "\",\n";
        ss << "      \"core_id\": " << d.core_id << ",\n";
        ss << "      \"sequence_number\": " << d.sequence_number << ",\n";
        ss << "      \"file_offset\": " << d.file_offset << ",\n";
        ss << "      \"event_type\": " << d.event_type << ",\n";
        ss << "      \"message\": \"" << escape_json_string(d.message) << "\",\n";
        ss << "      \"is_definitely_invalid\": " << (d.is_definitely_invalid ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < all_diags.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

} // namespace winios::trace
