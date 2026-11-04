#include "trace_analyzer.h"
#include "trace_types.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <set>
#include <sstream>

namespace winios::trace {

std::string TraceAnalyzer::decode_event_details(const TraceEvent& ev) {
    std::ostringstream ss;
    switch (ev.get_event_type()) {
        case TraceEventType::InstructionRetired:
            ss << "PC=0x" << std::hex << ev.pc << " opcode=0x" << ev.opcode_or_syndrome;
            break;
        case TraceEventType::BranchTaken:
            ss << "from=0x" << std::hex << ev.pc << " -> target=0x" << ev.address;
            break;
        case TraceEventType::ExceptionEntered:
            ss << "at PC=0x" << std::hex << ev.pc << " vector=0x" << ev.address
               << " ESR=0x" << ev.opcode_or_syndrome;
            if (ev.has_flag(TraceEventFlags::HasData)) {
                ss << " FAR=0x" << ev.data;
            }
            if (ev.has_flag(TraceEventFlags::IsSynchronous)) {
                ss << " [Sync]";
            } else if (ev.has_flag(TraceEventFlags::IsInterrupt)) {
                ss << " [Interrupt]";
            }
            break;
        case TraceEventType::ExceptionReturned:
            ss << "ERET at 0x" << std::hex << ev.pc << " -> return_pc=0x" << ev.address;
            break;
        case TraceEventType::InterruptDelivered:
            ss << "INTID=" << std::dec << ev.opcode_or_syndrome
               << " at PC=0x" << std::hex << ev.pc << " vector=0x" << ev.address;
            break;
        case TraceEventType::SystemRegisterAccess:
            ss << (ev.has_flag(TraceEventFlags::IsWrite) ? "MSR" : "MRS")
               << " reg=0x" << std::hex << ev.address << " val=0x" << ev.data;
            break;
        case TraceEventType::MemoryRead:
            ss << "LDR [0x" << std::hex << ev.address << "] = 0x" << ev.data
               << " (size=" << std::dec << ev.access_size << "B)";
            if (ev.has_flag(TraceEventFlags::IsAtomic)) ss << " [Atomic]";
            break;
        case TraceEventType::MemoryWrite:
            ss << "STR [0x" << std::hex << ev.address << "] = 0x" << ev.data
               << " (size=" << std::dec << ev.access_size << "B)";
            if (ev.has_flag(TraceEventFlags::IsAtomic)) ss << " [Atomic]";
            break;
        case TraceEventType::DebugEvent:
            ss << "Debug hit at PC=0x" << std::hex << ev.pc;
            if (ev.has_flag(TraceEventFlags::IsBreakpoint)) ss << " [Breakpoint]";
            if (ev.has_flag(TraceEventFlags::IsWatchpoint)) ss << " [Watchpoint addr=0x" << ev.address << "]";
            if (ev.has_flag(TraceEventFlags::IsSingleStep)) ss << " [SingleStep]";
            break;
        case TraceEventType::CPUStarted:
            ss << "Core " << std::dec << ev.core_id << " Start entry=0x" << std::hex << ev.pc;
            break;
        case TraceEventType::CPUStopped:
            ss << "Core " << std::dec << ev.core_id << " Stop reason=" << std::dec << ev.opcode_or_syndrome;
            break;
        case TraceEventType::ContextChanged: {
            const unsigned prev_el = ev.data & 3u;
            const unsigned new_el = (ev.data >> 2u) & 3u;
            const unsigned reason = ev.opcode_or_syndrome;
            ss << "EL" << prev_el << " -> EL" << new_el << " (reason=" << reason << ") PC=0x" << std::hex << ev.pc;
            break;
        }
        case TraceEventType::TimerEvent: {
            const auto op = static_cast<std::uint8_t>(ev.opcode_or_syndrome & 0xFFu);
            const auto ttype = static_cast<std::uint8_t>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            const char* type_str = (ttype == 0) ? "Phys" : (ttype == 1) ? "Virt" : (ttype == 2) ? "Hyp" : "Unknown";
            const char* op_str = (op == 1) ? "CompareUpdate" : (op == 2) ? "ControlUpdate" :
                                 (op == 3) ? "Expired" : (op == 4) ? "InterruptAsserted" :
                                 (op == 5) ? "InterruptCleared" : (op == 6) ? "Reset" : "UnknownOp";
            ss << type_str << " " << op_str << " CVAL=0x" << std::hex << ev.address
               << " CNT=0x" << ev.data << " (flags=0x" << ev.opcode_or_syndrome << ")";
            break;
        }
        case TraceEventType::DroppedMarker:
            ss << "DroppedEvents count=" << std::dec << ev.data;
            break;
        case TraceEventType::TraceStart:
            ss << "Trace stream started";
            break;
        case TraceEventType::TraceEnd:
            ss << "Trace stream ended cleanly";
            break;
        default:
            ss << "Raw event_type=" << std::dec << ev.event_type;
            break;
    }
    return ss.str();
}

TraceFileInspectionResult TraceAnalyzer::inspect_file(const std::filesystem::path& path) const {
    TraceFileInspectionResult res{};
    res.file_path = path.string();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error,
            DiagnosticCode::FileTruncated,
            0, 0, 0, 0,
            "File does not exist or is not a regular file",
            true
        });
        return res;
    }

    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
        return res;
    }
    res.file_size = sz;

    if (sz < TRACE_FILE_HEADER_SIZE) {
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error,
            DiagnosticCode::FileTruncated,
            0, 0, 0, 0,
            "File size is smaller than the 64-byte trace file header",
            true
        });
        return res;
    }

    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"rb");
#else
    fp = std::fopen(path.string().c_str(), "rb");
#endif
    if (!fp) {
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
        return res;
    }

    // 1. Read & validate 64-byte Header
    std::uint8_t hdr_bytes[TRACE_FILE_HEADER_SIZE];
    if (std::fread(hdr_bytes, 1, TRACE_FILE_HEADER_SIZE, fp) != TRACE_FILE_HEADER_SIZE) {
        std::fclose(fp);
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::FileTruncated,
            0, 0, 0, 0, "Failed to read full 64-byte file header", true
        });
        return res;
    }

    TraceFileHeader hdr{};
    TraceFileError err = deserialize_file_header(hdr_bytes, sizeof(hdr_bytes), hdr);
    res.format_version = hdr.file_format_version;
    res.schema_version = hdr.schema_version;

    if (err == TraceFileError::InvalidMagic) {
        res.integrity_status = TraceFileIntegrityStatus::InvalidHeader;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::HeaderMagicMismatch,
            0, 0, 0, 0, "Header magic mismatch (expected WINIOSTR)", true
        });
        std::fclose(fp);
        return res;
    }
    if (err == TraceFileError::UnsupportedVersion || hdr.file_format_version != TRACE_FILE_FORMAT_VERSION) {
        res.integrity_status = TraceFileIntegrityStatus::UnsupportedVersion;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::HeaderVersionUnsupported,
            0, 0, 8, 0, "Unsupported trace file format version", true
        });
        std::fclose(fp);
        return res;
    }
    if (err == TraceFileError::ChecksumMismatch) {
        res.integrity_status = TraceFileIntegrityStatus::CrcMismatch;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::HeaderCrcMismatch,
            0, 0, 60, 0, "Header CRC32C checksum mismatch", true
        });
        std::fclose(fp);
        return res;
    }
    if (err != TraceFileError::Success) {
        res.integrity_status = TraceFileIntegrityStatus::InvalidHeader;
        res.diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::HeaderMagicMismatch,
            0, 0, 0, 0, "Header deserialization failed", true
        });
        std::fclose(fp);
        return res;
    }
    res.header_valid = true;

    // 2. Read & validate 64-byte Footer (if file is large enough)
    bool has_footer = false;
    TraceFileFooter ftr{};
    if (sz >= TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE) {
#if defined(_WIN32)
        _fseeki64(fp, static_cast<__int64>(sz - TRACE_FILE_FOOTER_SIZE), SEEK_SET);
#else
        std::fseek(fp, static_cast<long>(sz - TRACE_FILE_FOOTER_SIZE), SEEK_SET);
#endif
        std::uint8_t ftr_bytes[TRACE_FILE_FOOTER_SIZE];
        if (std::fread(ftr_bytes, 1, TRACE_FILE_FOOTER_SIZE, fp) == TRACE_FILE_FOOTER_SIZE) {
            err = deserialize_file_footer(ftr_bytes, sizeof(ftr_bytes), ftr);
            if (err == TraceFileError::Success) {
                res.footer_valid = true;
                has_footer = true;
                res.footer_expected_events = ftr.total_events;
                res.footer_expected_blocks = ftr.total_blocks;
            } else if (err == TraceFileError::ChecksumMismatch) {
                res.integrity_status = TraceFileIntegrityStatus::CrcMismatch;
                res.diagnostics.push_back({
                    DiagnosticSeverity::Error, DiagnosticCode::FooterCrcMismatch,
                    0, 0, sz - TRACE_FILE_FOOTER_SIZE, 0,
                    "Footer CRC32C checksum mismatch", true
                });
            } else {
                res.diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::FooterMagicMismatch,
                    0, 0, sz - TRACE_FILE_FOOTER_SIZE, 0,
                    "Footer magic mismatch or unfinalized trace footer", false
                });
            }
        }
    } else {
        res.diagnostics.push_back({
            DiagnosticSeverity::Warning, DiagnosticCode::FooterMissing,
            0, 0, sz, 0, "File too small to contain a finalization footer", false
        });
    }

    // 3. Scan all Blocks
    const std::uint64_t end_offset = (has_footer ? sz - TRACE_FILE_FOOTER_SIZE : sz);
    std::uint64_t current_offset = TRACE_FILE_HEADER_SIZE;
#if defined(_WIN32)
    _fseeki64(fp, static_cast<__int64>(current_offset), SEEK_SET);
#else
    std::fseek(fp, static_cast<long>(current_offset), SEEK_SET);
#endif

    std::vector<std::uint8_t> payload_buf;
    bool found_dropped_markers = false;
    bool found_unknown_events = false;
    bool found_crc_error = false;
    bool found_corrupt_block = false;
    bool found_truncation = false;

    while (current_offset < end_offset) {
        if (!has_footer && sz >= TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE &&
            current_offset == sz - TRACE_FILE_FOOTER_SIZE) {
            break;
        }

        if (current_offset + TRACE_BLOCK_HEADER_SIZE > end_offset) {
            found_truncation = true;
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockTruncated,
                0, 0, current_offset, 0, "Truncated block header at end of stream", true
            });
            break;
        }

        std::uint8_t blk_hdr_bytes[TRACE_BLOCK_HEADER_SIZE];
        if (std::fread(blk_hdr_bytes, 1, TRACE_BLOCK_HEADER_SIZE, fp) != TRACE_BLOCK_HEADER_SIZE) {
            found_truncation = true;
            break;
        }

        TraceBlockHeader blk{};
        err = deserialize_block_header(blk_hdr_bytes, sizeof(blk_hdr_bytes), blk);
        if (err == TraceFileError::InvalidMagic) {
            found_corrupt_block = true;
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockMagicMismatch,
                0, 0, current_offset, 0, "Block header magic mismatch (expected TBLK)", true
            });
            break;
        }
        if (err == TraceFileError::ChecksumMismatch) {
            found_crc_error = true;
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockHeaderCrcMismatch,
                0, 0, current_offset, 0, "Block header CRC32C mismatch", true
            });
            break;
        }
        if (err != TraceFileError::Success) {
            found_corrupt_block = true;
            break;
        }

        if (blk.block_sequence != res.block_count) {
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockSequenceMismatch,
                0, 0, current_offset, 0, "Block sequence number discontinuity", true
            });
        }

        if (current_offset + TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes > end_offset) {
            found_truncation = true;
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockTruncated,
                0, 0, current_offset, 0, "Block payload exceeds file boundary (truncated payload)", true
            });
            break;
        }

        payload_buf.resize(blk.payload_bytes);
        if (std::fread(payload_buf.data(), 1, blk.payload_bytes, fp) != blk.payload_bytes) {
            found_truncation = true;
            break;
        }

        // Check payload CRC32C
        const std::uint32_t computed_crc = calculate_crc32c(payload_buf.data(), blk.payload_bytes);
        if (computed_crc != blk.payload_crc32c) {
            found_crc_error = true;
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockPayloadCrcMismatch,
                0, 0, current_offset + TRACE_BLOCK_HEADER_SIZE, 0,
                "Block payload CRC32C mismatch (corrupted block data)", true
            });
        }

        // Inspect events inside block
        for (std::uint32_t i = 0; i < blk.event_count; ++i) {
            TraceEvent ev{};
            err = deserialize_trace_event(
                payload_buf.data() + (i * TRACE_EVENT_DISK_SIZE), TRACE_EVENT_DISK_SIZE, ev);
            if (err != TraceFileError::Success) {
                found_corrupt_block = true;
                break;
            }

            // Check for DroppedMarker
            if (ev.get_event_type() == TraceEventType::DroppedMarker) {
                found_dropped_markers = true;
                res.dropped_markers_count++;
                res.diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::DroppedMarkerEncountered,
                    ev.core_id, ev.sequence_number, current_offset + TRACE_BLOCK_HEADER_SIZE + i * 64,
                    ev.event_type, "DroppedMarker detected: ring buffer experienced drop", false
                });
            }

            // Check for Unknown/Unallocated Event Type
            const auto raw_type = ev.event_type;
            if (raw_type == 0 || (raw_type >= 14 && raw_type <= 199) || (raw_type >= 203)) {
                found_unknown_events = true;
                res.unknown_event_types_count++;
                res.diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::UnknownEventTypeEncountered,
                    ev.core_id, ev.sequence_number, current_offset + TRACE_BLOCK_HEADER_SIZE + i * 64,
                    ev.event_type, "Unknown or unallocated trace event type encountered", false
                });
            }

            if (res.event_count == 0 || ev.virtual_time < res.trace_start_time) {
                res.trace_start_time = ev.virtual_time;
            }
            if (ev.virtual_time > res.trace_end_time) {
                res.trace_end_time = ev.virtual_time;
            }

            res.per_core_event_counts[ev.core_id]++;
            res.event_count++;
        }

        res.block_count++;
        current_offset += TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes;
    }

    std::fclose(fp);

    // 4. Validate stream accounting invariants against footer
    if (res.footer_valid) {
        if (res.event_count != res.footer_expected_events) {
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::EventCountMismatch,
                0, 0, sz - TRACE_FILE_FOOTER_SIZE, 0,
                "Observed event count does not match footer total_events", true
            });
            found_truncation = true;
        }
        if (res.block_count != res.footer_expected_blocks) {
            res.diagnostics.push_back({
                DiagnosticSeverity::Error, DiagnosticCode::BlockCountMismatch,
                0, 0, sz - TRACE_FILE_FOOTER_SIZE, 0,
                "Observed block count does not match footer total_blocks", true
            });
            found_truncation = true;
        }
    }

    // 5. Final integrity classification
    res.crc_valid = !found_crc_error;
    if (found_crc_error) {
        res.integrity_status = TraceFileIntegrityStatus::CrcMismatch;
    } else if (found_truncation) {
        res.integrity_status = TraceFileIntegrityStatus::TruncatedTrace;
    } else if (found_corrupt_block) {
        res.integrity_status = TraceFileIntegrityStatus::InvalidBlockHeader;
    } else if (!res.footer_valid) {
        res.integrity_status = TraceFileIntegrityStatus::InvalidFooter;
    } else if (found_unknown_events) {
        res.integrity_status = TraceFileIntegrityStatus::UnknownEventType;
    } else if (found_dropped_markers) {
        res.integrity_status = TraceFileIntegrityStatus::ContainsDroppedMarkers;
    } else if (res.event_count == 0) {
        res.integrity_status = TraceFileIntegrityStatus::ValidNoEvents;
    } else {
        res.integrity_status = TraceFileIntegrityStatus::ValidComplete;
    }

    return res;
}

void TraceAnalyzer::process_event(
    const TraceEvent& ev,
    std::uint64_t file_index,
    TraceAnalysisReport& report,
    std::map<std::uint16_t, std::uint64_t>& last_seq_per_core,
    std::map<std::uint16_t, bool>& cpu_started_per_core,
    std::map<std::uint16_t, bool>& cpu_stopped_per_core,
    std::map<std::uint16_t, std::uint32_t>& in_exception_depth_per_core,
    std::map<std::uint16_t, bool>& pending_timer_irq_per_core,
    std::map<std::pair<std::uint16_t, std::uint8_t>, TimerInterruptChain>& active_timer_chains) {

    (void)file_index;
    // 1. Per-core summary aggregation
    auto it_core = std::find_if(
        report.core_summaries.begin(), report.core_summaries.end(),
        [&](const PerCoreSummary& s) { return s.core_id == ev.core_id; });

    if (it_core == report.core_summaries.end()) {
        PerCoreSummary s{};
        s.core_id = ev.core_id;
        s.first_sequence = ev.sequence_number;
        s.last_sequence = ev.sequence_number;
        s.first_timestamp = ev.virtual_time;
        s.last_timestamp = ev.virtual_time;
        s.event_count = 1;
        s.type_counts[ev.get_event_type()] = 1;
        report.core_summaries.push_back(s);
    } else {
        it_core->event_count++;
        it_core->last_sequence = ev.sequence_number;
        it_core->last_timestamp = ev.virtual_time;
        it_core->type_counts[ev.get_event_type()]++;
    }

    // 2. Subsystem summaries
    switch (ev.get_event_type()) {
        case TraceEventType::InstructionRetired:
            report.instructions.total_retired++;
            report.instructions.per_core_count[ev.core_id]++;
            if (report.instructions.min_pc == 0 || ev.pc < report.instructions.min_pc) {
                report.instructions.min_pc = ev.pc;
            }
            if (ev.pc > report.instructions.max_pc) {
                report.instructions.max_pc = ev.pc;
            }
            if (report.instructions.total_retired == 1) {
                report.instructions.first_pc = ev.pc;
            }
            report.instructions.last_pc = ev.pc;
            break;

        case TraceEventType::BranchTaken:
            report.branches.total_branches++;
            report.branches.per_core_count[ev.core_id]++;
            if (ev.address > ev.pc) {
                report.branches.forward_branches++;
            } else if (ev.address != 0 && ev.address <= ev.pc) {
                report.branches.backward_branches++;
            }
            if (ev.address == 0) {
                report.branches.branches_without_target++;
            }
            break;

        case TraceEventType::ExceptionEntered:
            report.exceptions.total_entries++;
            if (ev.exception_level < 4) {
                report.exceptions.entries_per_el[ev.exception_level]++;
            }
            if (ev.has_flag(TraceEventFlags::IsSynchronous)) {
                report.exceptions.synchronous_entries++;
            }
            if (ev.has_flag(TraceEventFlags::IsInterrupt)) {
                report.exceptions.asynchronous_entries++;
            }
            in_exception_depth_per_core[ev.core_id]++;
            break;

        case TraceEventType::ExceptionReturned:
            report.exceptions.total_returns++;
            if (ev.exception_level < 4) {
                report.exceptions.returns_per_el[ev.exception_level]++;
            }
            if (in_exception_depth_per_core[ev.core_id] > 0) {
                in_exception_depth_per_core[ev.core_id]--;
            } else {
                report.exceptions.unmatched_returns++;
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    DiagnosticCode::ExceptionReturnWithoutEntry,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "ExceptionReturned observed without preceding ExceptionEntered",
                    false
                });
            }
            break;

        case TraceEventType::MemoryRead:
            report.memory.total_reads++;
            report.memory.per_core_reads[ev.core_id]++;
            if (ev.has_flag(TraceEventFlags::IsAtomic)) report.memory.total_atomics++;
            if (ev.access_size == 1) report.memory.size_1b++;
            else if (ev.access_size == 2) report.memory.size_2b++;
            else if (ev.access_size == 4) report.memory.size_4b++;
            else if (ev.access_size == 8) report.memory.size_8b++;
            else if (ev.access_size == 16) report.memory.size_16b++;
            else report.memory.size_other++;
            if (report.memory.min_address == 0 || ev.address < report.memory.min_address) {
                report.memory.min_address = ev.address;
            }
            if (ev.address > report.memory.max_address) {
                report.memory.max_address = ev.address;
            }
            break;

        case TraceEventType::MemoryWrite:
            report.memory.total_writes++;
            report.memory.per_core_writes[ev.core_id]++;
            if (ev.has_flag(TraceEventFlags::IsAtomic)) report.memory.total_atomics++;
            if (ev.access_size == 1) report.memory.size_1b++;
            else if (ev.access_size == 2) report.memory.size_2b++;
            else if (ev.access_size == 4) report.memory.size_4b++;
            else if (ev.access_size == 8) report.memory.size_8b++;
            else if (ev.access_size == 16) report.memory.size_16b++;
            else report.memory.size_other++;
            if (report.memory.min_address == 0 || ev.address < report.memory.min_address) {
                report.memory.min_address = ev.address;
            }
            if (ev.address > report.memory.max_address) {
                report.memory.max_address = ev.address;
            }
            break;

        case TraceEventType::SystemRegisterAccess:
            report.sysregs.total_accesses++;
            report.sysregs.per_core_count[ev.core_id]++;
            if (ev.has_flag(TraceEventFlags::IsWrite)) {
                report.sysregs.total_writes++;
            } else {
                report.sysregs.total_reads++;
            }
            report.sysregs.register_counts[ev.address]++;
            break;

        case TraceEventType::TimerEvent: {
            const auto op = static_cast<std::uint8_t>(ev.opcode_or_syndrome & 0xFFu);
            const auto ttype = static_cast<std::uint8_t>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
            if (ttype < 3) report.timers.per_type_counts[ttype]++;
            switch (op) {
                case 1: report.timers.compare_updates++; break;
                case 2: report.timers.control_updates++; break;
                case 3: report.timers.expirations++; break;
                case 4: report.timers.interrupt_assertions++; break;
                case 5: report.timers.interrupt_clears++; break;
                case 6: report.timers.resets++; break;
                default: break;
            }
            break;
        }

        case TraceEventType::CPUStarted:
            if (cpu_started_per_core[ev.core_id] && !cpu_stopped_per_core[ev.core_id]) {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::DuplicateCPUStart,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "Duplicate CPUStarted event without intervening stop or reset", false
                });
            }
            cpu_started_per_core[ev.core_id] = true;
            cpu_stopped_per_core[ev.core_id] = false;
            break;

        case TraceEventType::CPUStopped:
            if (!cpu_started_per_core[ev.core_id]) {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Info, DiagnosticCode::CPUStoppedWithoutStart,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "CPUStopped observed without preceding CPUStarted in trace window", false
                });
            } else if (cpu_stopped_per_core[ev.core_id]) {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::DuplicateCPUStop,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "Duplicate CPUStopped event without intervening start", false
                });
            }
            cpu_stopped_per_core[ev.core_id] = true;
            break;

        case TraceEventType::ContextChanged: {
            const unsigned prev_el = ev.data & 0x3u;
            const unsigned raw_new_el = (ev.data >> 2u) & 0xFu;
            if (prev_el > 3 || raw_new_el > 3 || ev.exception_level > 3) {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Error, DiagnosticCode::ContextChangeIncompleteMetadata,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "ContextChanged metadata contains out-of-range exception level", true
                });
            }
            break;
        }

        default:
            break;
    }

    // 3. Per-core sequence monotonicity validation
    auto it_seq = last_seq_per_core.find(ev.core_id);
    if (it_seq != last_seq_per_core.end()) {
        if (ev.sequence_number <= it_seq->second) {
            report.instructions.sequence_regressions++;
            report.sequence_diagnostics.push_back({
                DiagnosticSeverity::Warning, DiagnosticCode::SequenceRegression,
                ev.core_id, ev.sequence_number, 0, ev.event_type,
                "Sequence number regressed or duplicated on core", true
            });
        } else if (ev.sequence_number > it_seq->second + 1) {
            report.instructions.sequence_gaps++;
            report.sequence_diagnostics.push_back({
                DiagnosticSeverity::Info, DiagnosticCode::SequenceGap,
                ev.core_id, ev.sequence_number, 0, ev.event_type,
                "Sequence gap observed on core", false
            });
        }
    }
    last_seq_per_core[ev.core_id] = ev.sequence_number;

    // 4. Timer to Interrupt Causal Analysis State Machine
    if (ev.get_event_type() == TraceEventType::TimerEvent) {
        const auto op = static_cast<std::uint8_t>(ev.opcode_or_syndrome & 0xFFu);
        const auto ttype = static_cast<std::uint8_t>((ev.opcode_or_syndrome >> 8u) & 0xFFu);
        const auto key = std::make_pair(ev.core_id, ttype);

        if (op == 3) { // Expired
            TimerInterruptChain chain{};
            chain.core_id = ev.core_id;
            chain.timer_type = ttype;
            chain.expire_seq = ev.sequence_number;
            chain.expire_time = ev.virtual_time;
            chain.status = CausalCorrelationStatus::IncompleteSequence;
            chain.explanation = "Timer expired; awaiting interrupt assertion";
            active_timer_chains[key] = chain;
        } else if (op == 4) { // InterruptAsserted
            auto cit = active_timer_chains.find(key);
            if (cit != active_timer_chains.end() && cit->second.expire_seq > 0) {
                cit->second.assert_seq = ev.sequence_number;
                cit->second.assert_time = ev.virtual_time;
                cit->second.status = CausalCorrelationStatus::IncompleteSequence;
                cit->second.explanation = "Timer asserted; awaiting GIC delivery";
            } else {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::TimerAssertWithoutExpired,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "Timer interrupt assertion without preceding expiration", false
                });
                TimerInterruptChain chain{};
                chain.core_id = ev.core_id;
                chain.timer_type = ttype;
                chain.assert_seq = ev.sequence_number;
                chain.assert_time = ev.virtual_time;
                chain.status = CausalCorrelationStatus::IncompleteSequence;
                chain.explanation = "Timer asserted without observed expiration";
                active_timer_chains[key] = chain;
            }
            pending_timer_irq_per_core[ev.core_id] = true;
        } else if (op == 5) { // InterruptCleared
            pending_timer_irq_per_core[ev.core_id] = false;
            auto cit = active_timer_chains.find(key);
            if (cit != active_timer_chains.end() && cit->second.deliver_seq == 0) {
                cit->second.status = CausalCorrelationStatus::IncompleteSequence;
                cit->second.explanation = "Timer cleared/masked before delivery occurred";
                report.causal_chains.push_back(cit->second);
                active_timer_chains.erase(cit);
            }
        }
    } else if (ev.get_event_type() == TraceEventType::InterruptDelivered) {
        const std::uint32_t intid = (ev.data != 0 ? static_cast<std::uint32_t>(ev.data) : ev.opcode_or_syndrome);
        const std::uint8_t ttype = (intid == 30 ? 0 : (intid == 27 ? 1 : (intid == 26 ? 2 : 255)));
        if (ttype != 255) {
            const auto key = std::make_pair(ev.core_id, ttype);
            auto cit = active_timer_chains.find(key);
            if (cit != active_timer_chains.end() && cit->second.assert_seq > 0) {
                cit->second.deliver_seq = ev.sequence_number;
                cit->second.deliver_time = ev.virtual_time;
                cit->second.intid = intid;
                cit->second.status = CausalCorrelationStatus::PossibleCorrelation;
                cit->second.explanation = "Interrupt delivered; awaiting vector exception entry";
            } else {
                report.sequence_diagnostics.push_back({
                    DiagnosticSeverity::Warning, DiagnosticCode::InterruptWithoutAssertion,
                    ev.core_id, ev.sequence_number, 0, ev.event_type,
                    "InterruptDelivered observed without prior assertion context", false
                });
                TimerInterruptChain uncorr{};
                uncorr.core_id = ev.core_id;
                uncorr.timer_type = ttype;
                uncorr.deliver_seq = ev.sequence_number;
                uncorr.deliver_time = ev.virtual_time;
                uncorr.intid = intid;
                uncorr.status = CausalCorrelationStatus::UncorrelatedEvent;
                uncorr.explanation = "Delivery without observed matching timer assertion";
                report.causal_chains.push_back(uncorr);
            }
        }
    } else if (ev.get_event_type() == TraceEventType::ExceptionEntered &&
               ev.has_flag(TraceEventFlags::IsInterrupt)) {
        for (auto& pair : active_timer_chains) {
            if (pair.first.first == ev.core_id && pair.second.deliver_seq > 0 && pair.second.entry_seq == 0) {
                pair.second.entry_seq = ev.sequence_number;
                pair.second.entry_time = ev.virtual_time;
                pair.second.entry_vector = ev.address;
                break;
            }
        }
    } else if (ev.get_event_type() == TraceEventType::ContextChanged &&
               ev.has_flag(TraceEventFlags::IsInterrupt)) {
        for (auto it = active_timer_chains.begin(); it != active_timer_chains.end();) {
            if (it->first.first == ev.core_id && it->second.entry_seq > 0 && it->second.context_change_seq == 0) {
                it->second.context_change_seq = ev.sequence_number;
                it->second.context_change_time = ev.virtual_time;
                it->second.status = CausalCorrelationStatus::ConfirmedSequence;
                it->second.explanation = "Confirmed causal sequence: Expired -> Assert -> Deliver -> Entry -> ContextChanged";
                report.causal_chains.push_back(it->second);
                it = active_timer_chains.erase(it);
            } else {
                ++it;
            }
        }
    }
}

TraceAnalysisReport TraceAnalyzer::analyze_file(
    const std::filesystem::path& path,
    const TraceFilter& filter,
    std::size_t max_timeline_events) const {

    TraceAnalysisReport report{};
    report.inspection = inspect_file(path);
    report.active_filter = filter;

    // Check filter validity
    std::string filter_err;
    if (!filter.is_valid(&filter_err)) {
        report.sequence_diagnostics.push_back({
            DiagnosticSeverity::Error, DiagnosticCode::Ok,
            0, 0, 0, 0, "Invalid filter configuration: " + filter_err, true
        });
        return report;
    }

    if (!report.inspection.header_valid) {
        return report;
    }

    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"rb");
#else
    fp = std::fopen(path.string().c_str(), "rb");
#endif
    if (!fp) return report;

    const std::uint64_t sz = report.inspection.file_size;
    const std::uint64_t end_offset = (report.inspection.footer_valid ?
        sz - TRACE_FILE_FOOTER_SIZE : sz);
    std::uint64_t current_offset = TRACE_FILE_HEADER_SIZE;
#if defined(_WIN32)
    _fseeki64(fp, static_cast<__int64>(current_offset), SEEK_SET);
#else
    std::fseek(fp, static_cast<long>(current_offset), SEEK_SET);
#endif

    std::vector<std::uint8_t> payload_buf;
    std::uint64_t global_file_index = 0;
    std::set<std::uint64_t> unique_pcs;

    // Transient state tracking maps
    std::map<std::uint16_t, std::uint64_t> last_seq_per_core;
    std::map<std::uint16_t, bool> cpu_started_per_core;
    std::map<std::uint16_t, bool> cpu_stopped_per_core;
    std::map<std::uint16_t, std::uint32_t> in_exception_depth_per_core;
    std::map<std::uint16_t, bool> pending_timer_irq_per_core;
    std::map<std::pair<std::uint16_t, std::uint8_t>, TimerInterruptChain> active_timer_chains;

    while (current_offset < end_offset) {
        if (end_offset == sz && sz >= TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE &&
            current_offset == sz - TRACE_FILE_FOOTER_SIZE) {
            break;
        }

        if (current_offset + TRACE_BLOCK_HEADER_SIZE > end_offset) break;

        std::uint8_t blk_hdr_bytes[TRACE_BLOCK_HEADER_SIZE];
        if (std::fread(blk_hdr_bytes, 1, TRACE_BLOCK_HEADER_SIZE, fp) != TRACE_BLOCK_HEADER_SIZE) break;

        TraceBlockHeader blk{};
        if (deserialize_block_header(blk_hdr_bytes, sizeof(blk_hdr_bytes), blk) != TraceFileError::Success) break;
        if (current_offset + TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes > end_offset) break;

        payload_buf.resize(blk.payload_bytes);
        if (std::fread(payload_buf.data(), 1, blk.payload_bytes, fp) != blk.payload_bytes) break;

        for (std::uint32_t i = 0; i < blk.event_count; ++i) {
            TraceEvent ev{};
            if (deserialize_trace_event(payload_buf.data() + (i * TRACE_EVENT_DISK_SIZE),
                                        TRACE_EVENT_DISK_SIZE, ev) != TraceFileError::Success) {
                continue;
            }

            if (filter.matches(ev)) {
                report.filtered_events_count++;
                if (unique_pcs.size() < 100000) {
                    unique_pcs.insert(ev.pc);
                }

                process_event(ev, global_file_index, report,
                              last_seq_per_core, cpu_started_per_core, cpu_stopped_per_core,
                              in_exception_depth_per_core, pending_timer_irq_per_core,
                              active_timer_chains);

                if (report.sample_timeline.size() < max_timeline_events) {
                    TimelineEntry te{};
                    te.file_index = global_file_index;
                    te.sequence_number = ev.sequence_number;
                    te.core_id = ev.core_id;
                    te.timestamp = ev.virtual_time;
                    te.event_type = ev.get_event_type();
                    te.exception_level = ev.exception_level;
                    te.pc = ev.pc;
                    te.address = ev.address;
                    te.data = ev.data;
                    te.opcode_or_syndrome = ev.opcode_or_syndrome;
                    te.flags = ev.flags;
                    te.decoded_info = decode_event_details(ev);
                    report.sample_timeline.push_back(te);
                }
            }
            global_file_index++;
        }
        current_offset += TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes;
    }

    std::fclose(fp);

    report.instructions.unique_pc_count = unique_pcs.size();

    // Check for unmatched exception entries
    for (const auto& pair : in_exception_depth_per_core) {
        if (pair.second > 0) {
            report.exceptions.unmatched_entries += pair.second;
            report.sequence_diagnostics.push_back({
                DiagnosticSeverity::Info, DiagnosticCode::UnmatchedExceptionEntry,
                pair.first, 0, 0, static_cast<std::uint16_t>(TraceEventType::ExceptionEntered),
                "Exception entry without observed matching return in trace window", false
            });
        }
    }

    // Flush any remaining active timer chains as incomplete
    for (const auto& pair : active_timer_chains) {
        report.causal_chains.push_back(pair.second);
    }

    return report;
}

std::vector<TimelineEntry> TraceAnalyzer::extract_timeline(
    const std::filesystem::path& path,
    const TraceFilter& filter,
    std::size_t max_events) const {

    std::vector<TimelineEntry> results;
    stream_timeline(path, filter, [&](const TimelineEntry& entry) {
        results.push_back(entry);
        if (max_events > 0 && results.size() >= max_events) {
            return false; // Stop streaming
        }
        return true;
    });
    return results;
}

bool TraceAnalyzer::stream_timeline(
    const std::filesystem::path& path,
    const TraceFilter& filter,
    const std::function<bool(const TimelineEntry&)>& consumer) const {

    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"rb");
#else
    fp = std::fopen(path.string().c_str(), "rb");
#endif
    if (!fp) return false;

    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < TRACE_FILE_HEADER_SIZE) {
        std::fclose(fp);
        return false;
    }

    // Check header
    std::uint8_t hdr_bytes[TRACE_FILE_HEADER_SIZE];
    if (std::fread(hdr_bytes, 1, TRACE_FILE_HEADER_SIZE, fp) != TRACE_FILE_HEADER_SIZE) {
        std::fclose(fp);
        return false;
    }
    TraceFileHeader hdr{};
    if (deserialize_file_header(hdr_bytes, sizeof(hdr_bytes), hdr) != TraceFileError::Success) {
        std::fclose(fp);
        return false;
    }

    // Determine end offset (respecting footer if present)
    std::uint64_t end_offset = sz;
    if (sz >= TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE) {
#if defined(_WIN32)
        _fseeki64(fp, static_cast<__int64>(sz - TRACE_FILE_FOOTER_SIZE), SEEK_SET);
#else
        std::fseek(fp, static_cast<long>(sz - TRACE_FILE_FOOTER_SIZE), SEEK_SET);
#endif
        std::uint8_t ftr_bytes[TRACE_FILE_FOOTER_SIZE];
        if (std::fread(ftr_bytes, 1, TRACE_FILE_FOOTER_SIZE, fp) == TRACE_FILE_FOOTER_SIZE) {
            TraceFileFooter ftr{};
            if (deserialize_file_footer(ftr_bytes, sizeof(ftr_bytes), ftr) == TraceFileError::Success) {
                end_offset = sz - TRACE_FILE_FOOTER_SIZE;
            }
        }
    }

    std::uint64_t current_offset = TRACE_FILE_HEADER_SIZE;
#if defined(_WIN32)
    _fseeki64(fp, static_cast<__int64>(current_offset), SEEK_SET);
#else
    std::fseek(fp, static_cast<long>(current_offset), SEEK_SET);
#endif

    std::vector<std::uint8_t> payload_buf;
    std::uint64_t global_file_index = 0;

    while (current_offset < end_offset) {
        if (end_offset == sz && sz >= TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE &&
            current_offset == sz - TRACE_FILE_FOOTER_SIZE) {
            break;
        }

        if (current_offset + TRACE_BLOCK_HEADER_SIZE > end_offset) break;

        std::uint8_t blk_hdr_bytes[TRACE_BLOCK_HEADER_SIZE];
        if (std::fread(blk_hdr_bytes, 1, TRACE_BLOCK_HEADER_SIZE, fp) != TRACE_BLOCK_HEADER_SIZE) break;

        TraceBlockHeader blk{};
        if (deserialize_block_header(blk_hdr_bytes, sizeof(blk_hdr_bytes), blk) != TraceFileError::Success) break;
        if (current_offset + TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes > end_offset) break;

        payload_buf.resize(blk.payload_bytes);
        if (std::fread(payload_buf.data(), 1, blk.payload_bytes, fp) != blk.payload_bytes) break;

        for (std::uint32_t i = 0; i < blk.event_count; ++i) {
            TraceEvent ev{};
            if (deserialize_trace_event(payload_buf.data() + (i * TRACE_EVENT_DISK_SIZE),
                                        TRACE_EVENT_DISK_SIZE, ev) != TraceFileError::Success) {
                continue;
            }

            if (filter.matches(ev)) {
                TimelineEntry te{};
                te.file_index = global_file_index;
                te.sequence_number = ev.sequence_number;
                te.core_id = ev.core_id;
                te.timestamp = ev.virtual_time;
                te.event_type = ev.get_event_type();
                te.exception_level = ev.exception_level;
                te.pc = ev.pc;
                te.address = ev.address;
                te.data = ev.data;
                te.opcode_or_syndrome = ev.opcode_or_syndrome;
                te.flags = ev.flags;
                te.decoded_info = decode_event_details(ev);

                if (!consumer(te)) {
                    std::fclose(fp);
                    return true;
                }
            }
            global_file_index++;
        }
        current_offset += TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes;
    }

    std::fclose(fp);
    return true;
}

} // namespace winios::trace
