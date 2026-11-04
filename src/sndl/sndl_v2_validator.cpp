#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_format.h"
#include "snapshot/smp_snapshot_manager.h"
#include "trace/trace_serializer.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::sndl {

SndlV2ValidationReport SndlV2Validator::validate_file(
    const std::filesystem::path& sndl_path,
    const std::optional<std::filesystem::path>& baseline_snapshot_path,
    const SndlV2ValidationOptions& options) {
    SndlV2ValidationReport rep{};

    std::ifstream file(sndl_path, std::ios::binary);
    if (!file.is_open()) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::FileOpenFailed;
        rep.message = "Failed to open SNDL v2 file: " + sndl_path.string();
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    file.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(hdr_bytes))) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::FileTooShort;
        rep.message = "File too short for SNDL v2 header";
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    WSndlV2Header hdr{};
    const auto hdr_res = decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), hdr);
    if (hdr_res.is_failure()) {
        rep.is_valid = false;
        rep.code = hdr_res.code;
        rep.message = hdr_res.message;
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    rep.core_count = hdr.core_count;
    rep.base_snapshot_hash = hdr.base_snapshot_hash;
    rep.diagnostics.push_back("Header verified: version=2, core_count=" + std::to_string(hdr.core_count) +
                              ", snapshot_hash=0x" + [h = hdr.base_snapshot_hash] {
                                  std::ostringstream ss;
                                  ss << std::hex << h;
                                  return ss.str();
                              }());

    // Snapshot verification with SmpSnapshotManager
    if (baseline_snapshot_path.has_value() && !baseline_snapshot_path->empty()) {
        rep.snapshot_checked = true;
        std::error_code ec;
        if (!std::filesystem::exists(*baseline_snapshot_path, ec)) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotNotFound;
            rep.message = "Baseline snapshot file not found: " + baseline_snapshot_path->string();
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        winios::snapshot::SmpSnapshotInspection snap_info{};
        const auto inspect_res = winios::snapshot::SmpSnapshotManager::inspect_file(*baseline_snapshot_path, snap_info);
        if (!inspect_res.success) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotMismatch;
            rep.message = "Snapshot malformed: " + inspect_res.error_message;
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (snap_info.header.version < 2) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotMismatch;
            rep.message = "Snapshot format version mismatch: expected >= 2, got " + std::to_string(snap_info.header.version);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (snap_info.header.core_count != hdr.core_count) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotMismatch;
            rep.message = "Snapshot core count mismatch: snapshot has " + std::to_string(snap_info.header.core_count) +
                          ", SNDL has " + std::to_string(hdr.core_count);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (hdr.memory_capacity != 0 && snap_info.header.memory_capacity != 0 && snap_info.header.memory_capacity != hdr.memory_capacity) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotMismatch;
            rep.message = "Snapshot memory capacity mismatch between .wsnap and SNDL v2 header";
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (hdr.base_snapshot_hash != 0 && snap_info.integrity_data.canonical_cluster_hash != 0 &&
            hdr.base_snapshot_hash != snap_info.integrity_data.canonical_cluster_hash) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::SnapshotMismatch;
            rep.message = "Snapshot canonical cluster hash mismatch: snapshot has 0x" +
                          [h = snap_info.integrity_data.canonical_cluster_hash] {
                              std::ostringstream ss; ss << std::hex << h; return ss.str();
                          }() + ", SNDL specifies 0x" + [h = hdr.base_snapshot_hash] {
                              std::ostringstream ss; ss << std::hex << h; return ss.str();
                          }();
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        rep.snapshot_matched = true;
        rep.diagnostics.push_back("Baseline snapshot verified and matches SNDL header");
    }

    std::uint64_t last_seq = hdr.initial_global_sequence;
    std::uint64_t last_ret_seq = 0;
    std::vector<std::uint64_t> core_rets(hdr.core_count, 0);

    std::uint32_t blocks_read = 0;
    std::uint64_t events_read = 0;
    bool found_footer = false;
    WSndlV2Footer footer{};

    while (file) {
        // Peek 4 bytes to check for Block Magic or Footer Magic
        std::uint8_t magic_peek[4]{};
        const auto pos_before = file.tellg();
        file.read(reinterpret_cast<char*>(magic_peek), 4);
        if (file.gcount() == 0) {
            break; // Clean EOF
        }
        if (file.gcount() < 4) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::TruncatedBlock;
            rep.message = "Unexpected EOF reading magic identifier";
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        // Check if Footer Magic ('W', 'S', 'N', 'D')
        if (magic_peek[0] == 'W' && magic_peek[1] == 'S' && magic_peek[2] == 'N' && magic_peek[3] == 'D') {
            file.seekg(pos_before);
            std::uint8_t ftr_bytes[kWSndlV2FooterSize]{};
            file.read(reinterpret_cast<char*>(ftr_bytes), sizeof(ftr_bytes));
            if (file.gcount() < static_cast<std::streamsize>(sizeof(ftr_bytes))) {
                rep.is_valid = false;
                rep.code = SndlV2ErrorCode::TruncatedBlock;
                rep.message = "Truncated SNDL v2 footer";
                rep.diagnostics.push_back(rep.message);
                return rep;
            }

            const auto ftr_res = decode_sndl_v2_footer(ftr_bytes, sizeof(ftr_bytes), footer);
            if (ftr_res.is_failure()) {
                rep.is_valid = false;
                rep.code = ftr_res.code;
                rep.message = ftr_res.message;
                rep.diagnostics.push_back(rep.message);
                return rep;
            }

            found_footer = true;
            break;
        }

        // Otherwise, it must be a block header ('S', '2', 'B', 'K')
        file.seekg(pos_before);
        std::uint8_t blk_bytes[kWSndlV2BlockHeaderSize]{};
        file.read(reinterpret_cast<char*>(blk_bytes), sizeof(blk_bytes));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(blk_bytes))) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::TruncatedBlock;
            rep.message = "Truncated block header at block " + std::to_string(blocks_read);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        WSndlV2BlockHeader blk{};
        const auto blk_res = decode_sndl_v2_block_header(blk_bytes, sizeof(blk_bytes), blk);
        if (blk_res.is_failure()) {
            rep.is_valid = false;
            rep.code = blk_res.code;
            rep.message = "Block header failure at block " + std::to_string(blocks_read) + ": " + blk_res.message;
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (blk.block_index != blocks_read) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::InvalidSequence;
            rep.message = "Block index discontinuity: expected " + std::to_string(blocks_read) +
                          ", got " + std::to_string(blk.block_index);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (options.check_reserved_fields && blk.reserved != 0) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::MalformedRecord;
            rep.message = "Block header has non-zero reserved field at block " + std::to_string(blocks_read);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        if (blk.payload_bytes > options.max_block_payload_bytes) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::ResourceLimitExceeded;
            rep.message = "Block payload bytes exceeds limit (" + std::to_string(blk.payload_bytes) +
                          " > " + std::to_string(options.max_block_payload_bytes) + ")";
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        // Read payload
        const std::size_t payload_bytes = blk.payload_bytes;
        if (payload_bytes != blk.record_count * kWSndlV2RecordSize) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::MalformedRecord;
            rep.message = "Block payload byte count mismatch with record count";
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        std::vector<std::uint8_t> payload(payload_bytes);
        file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload_bytes));
        if (file.gcount() < static_cast<std::streamsize>(payload_bytes)) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::TruncatedBlock;
            rep.message = "Truncated block payload at block " + std::to_string(blocks_read);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        const std::uint32_t calc_payload_crc = trace::calculate_crc32c(payload.data(), payload_bytes);
        if (calc_payload_crc != blk.payload_crc32c) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::BlockPayloadCrcMismatch;
            rep.message = "Block payload CRC32C mismatch at block " + std::to_string(blocks_read);
            rep.diagnostics.push_back(rep.message);
            return rep;
        }

        // Decode records in block
        for (std::uint32_t r = 0; r < blk.record_count; ++r) {
            SndlV2Record rec{};
            const auto rec_res = decode_sndl_v2_record(payload.data() + r * kWSndlV2RecordSize, kWSndlV2RecordSize, rec);
            if (rec_res.is_failure()) {
                rep.is_valid = false;
                rep.code = rec_res.code;
                rep.message = "Record error in block " + std::to_string(blocks_read) + ", record " +
                              std::to_string(r) + ": " + rec_res.message;
                rep.diagnostics.push_back(rep.message);
                return rep;
            }

            if (events_read == 0) {
                if (rec.global_event_sequence < hdr.initial_global_sequence) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidSequence;
                    rep.message = "First record sequence (" + std::to_string(rec.global_event_sequence) +
                                  ") is less than initial_global_sequence (" + std::to_string(hdr.initial_global_sequence) + ")";
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }
                rep.first_global_sequence = rec.global_event_sequence;
            } else {
                if (options.enforce_strict_monotonicity) {
                    if (rec.global_event_sequence != last_seq + 1) {
                        rep.is_valid = false;
                        rep.code = SndlV2ErrorCode::InvalidSequence;
                        rep.message = "Global sequence non-consecutive: expected " + std::to_string(last_seq + 1) +
                                      ", got " + std::to_string(rec.global_event_sequence);
                        rep.diagnostics.push_back(rep.message);
                        return rep;
                    }
                } else if (rec.global_event_sequence <= last_seq) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidSequence;
                    rep.message = "Global sequence monotonicity violation: previous=" + std::to_string(last_seq) +
                                  ", current=" + std::to_string(rec.global_event_sequence);
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }
            }
            last_seq = rec.global_event_sequence;

            if (rec.global_retirement_sequence < last_ret_seq) {
                rep.is_valid = false;
                rep.code = SndlV2ErrorCode::InvalidSequence;
                rep.message = "Global retirement sequence backwards: previous=" + std::to_string(last_ret_seq) +
                              ", current=" + std::to_string(rec.global_retirement_sequence);
                rep.diagnostics.push_back(rep.message);
                return rep;
            }
            last_ret_seq = rec.global_retirement_sequence;

            if (rec.core_id >= hdr.core_count) {
                rep.is_valid = false;
                rep.code = SndlV2ErrorCode::InvalidCoreId;
                rep.message = "Record core_id " + std::to_string(rec.core_id) + " exceeds core_count " +
                              std::to_string(hdr.core_count);
                rep.diagnostics.push_back(rep.message);
                return rep;
            }

            if (rec.core_retirement_count < core_rets[rec.core_id]) {
                rep.is_valid = false;
                rep.code = SndlV2ErrorCode::InvalidSequence;
                rep.message = "Core " + std::to_string(rec.core_id) + " retirement count went backwards";
                rep.diagnostics.push_back(rep.message);
                return rep;
            }
            core_rets[rec.core_id] = rec.core_retirement_count;

            if (options.enable_semantic_validation) {
                // Opcode range validation
                const auto raw_type = static_cast<std::uint16_t>(rec.event_type);
                const bool valid_type =
                    (raw_type >= 10 && raw_type <= 16) || // Lifecycle
                    (raw_type >= 20 && raw_type <= 25) || // Scheduler
                    (raw_type >= 30 && raw_type <= 34) || // Interrupts / Exceptions
                    (raw_type >= 40 && raw_type <= 43) || // Timers
                    (raw_type >= 50 && raw_type <= 52) || // IO & Entropy
                    (raw_type >= 60 && raw_type <= 63);   // Shared Memory & Exclusives
                if (!valid_type) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::MalformedRecord;
                    rep.message = "Unknown event type opcode: " + std::to_string(raw_type);
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                // Boundary type validation
                const auto raw_bound = static_cast<std::uint8_t>(rec.boundary_type);
                if (raw_bound < 1 || raw_bound > 9) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::MalformedRecord;
                    rep.message = "Invalid boundary type: " + std::to_string(raw_bound);
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                // Reserved byte check
                if (options.check_reserved_fields && rec.reserved != 0) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::MalformedRecord;
                    rep.message = "Non-zero reserved field in record at global_seq " +
                                  std::to_string(rec.global_event_sequence);
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                // Payload size bounds
                if (rec.payload_size > 24) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidPayload;
                    rep.message = "Record payload_size exceeds 24 bytes limit";
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                // Semantic checks per event category
                if (rec.event_type == SndlV2EventType::QuantumStart && rec.payload1 == 0) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidPayload;
                    rep.message = "QuantumStart has zero quantum budget";
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                if (rec.event_type == SndlV2EventType::StoreExclusiveResult && rec.payload2 > 1) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidPayload;
                    rep.message = "StoreExclusiveResult status must be 0 or 1";
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }

                if (rec.event_type == SndlV2EventType::MmioReadResult &&
                    rec.payload3 != 1 && rec.payload3 != 2 && rec.payload3 != 4 && rec.payload3 != 8) {
                    rep.is_valid = false;
                    rep.code = SndlV2ErrorCode::InvalidPayload;
                    rep.message = "MmioReadResult invalid access width: " + std::to_string(rec.payload3);
                    rep.diagnostics.push_back(rep.message);
                    return rep;
                }
            }

            if (events_read == 0) {
                rep.first_global_sequence = rec.global_event_sequence;
            }
            rep.last_global_sequence = rec.global_event_sequence;
            rep.last_global_retirement_sequence = rec.global_retirement_sequence;
            events_read++;
        }

        blocks_read++;
    }

    rep.total_blocks = blocks_read;
    rep.total_events = events_read;

    if (!found_footer) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::IncompleteRecording;
        rep.message = "Recording has no footer (incomplete recording)";
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    if (footer.total_blocks != blocks_read) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::InvalidSequence;
        rep.message = "Footer total_blocks mismatch: footer=" + std::to_string(footer.total_blocks) +
                      ", actual=" + std::to_string(blocks_read);
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    if (footer.total_events != events_read) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::InvalidSequence;
        rep.message = "Footer total_events mismatch: footer=" + std::to_string(footer.total_events) +
                      ", actual=" + std::to_string(events_read);
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    // Footer overflow vs clean consistency checks
    if (footer.completion_status == SndlV2CompletionStatus::Overflow && footer.dropped_events == 0) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::MalformedRecord;
        rep.message = "Footer has Overflow status but 0 dropped events declared";
        rep.diagnostics.push_back(rep.message);
        return rep;
    }
    if (footer.completion_status == SndlV2CompletionStatus::Completed && footer.dropped_events > 0) {
        rep.is_valid = false;
        rep.code = SndlV2ErrorCode::MalformedRecord;
        rep.message = "Footer has Completed status but nonzero dropped events declared";
        rep.diagnostics.push_back(rep.message);
        return rep;
    }

    // Check for trailing garbage
    if (options.check_trailing_garbage) {
        char trailing_byte{};
        file.read(&trailing_byte, 1);
        if (file.gcount() > 0) {
            rep.is_valid = false;
            rep.code = SndlV2ErrorCode::TrailingGarbage;
            rep.message = "Unexpected trailing data after footer";
            rep.diagnostics.push_back(rep.message);
            return rep;
        }
    }

    rep.completion_status = footer.completion_status;
    rep.is_valid = true;
    rep.code = SndlV2ErrorCode::Success;
    rep.message = "SNDL v2 stream verified successfully (" + std::to_string(events_read) +
                  " events across " + std::to_string(blocks_read) + " blocks)";
    rep.diagnostics.push_back(rep.message);

    return rep;
}

SndlV2Result SndlV2Validator::inspect_file(
    const std::filesystem::path& sndl_path,
    SndlV2InspectionInfo& out_info) {
    out_info = {};

    std::error_code ec;
    out_info.file_size_bytes = std::filesystem::file_size(sndl_path, ec);
    if (ec) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed, "File not found: " + sndl_path.string());
    }

    std::ifstream file(sndl_path, std::ios::binary);
    if (!file.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed, "Failed to open file: " + sndl_path.string());
    }

    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    file.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(hdr_bytes))) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "File too short for SNDL v2 header");
    }

    const auto hdr_res = decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), out_info.header);
    if (hdr_res.is_failure()) {
        return hdr_res;
    }

    // Attempt to read footer from end of file
    if (out_info.file_size_bytes >= kWSndlV2HeaderSize + kWSndlV2FooterSize) {
        file.seekg(-static_cast<std::streamoff>(kWSndlV2FooterSize), std::ios::end);
        std::uint8_t ftr_bytes[kWSndlV2FooterSize]{};
        file.read(reinterpret_cast<char*>(ftr_bytes), sizeof(ftr_bytes));
        if (file.gcount() == sizeof(ftr_bytes)) {
            const auto ftr_res = decode_sndl_v2_footer(ftr_bytes, sizeof(ftr_bytes), out_info.footer);
            if (ftr_res.is_success()) {
                out_info.has_valid_footer = true;
                out_info.total_records = out_info.footer.total_events;
                out_info.total_blocks = out_info.footer.total_blocks;
                out_info.max_global_sequence = out_info.footer.last_global_sequence;
                out_info.max_global_retirement_sequence = out_info.footer.last_global_retirement_sequence;
                out_info.dropped_events = out_info.footer.dropped_events;
            }
        }
    }

    out_info.formatted_summary = format_summary(out_info);
    return SndlV2Result::ok();
}

SndlV2Result SndlV2Validator::extract_events(
    const std::filesystem::path& sndl_path,
    std::vector<SndlV2Record>& out_records,
    std::size_t max_records) {
    out_records.clear();

    std::ifstream file(sndl_path, std::ios::binary);
    if (!file.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed, "Failed to open file: " + sndl_path.string());
    }

    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    file.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(hdr_bytes))) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "File too short for header");
    }

    WSndlV2Header hdr{};
    const auto hdr_res = decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), hdr);
    if (hdr_res.is_failure()) {
        return hdr_res;
    }

    while (file) {
        std::uint8_t magic_peek[4]{};
        const auto pos_before = file.tellg();
        file.read(reinterpret_cast<char*>(magic_peek), 4);
        if (file.gcount() < 4) break;

        if (magic_peek[0] == 'W' && magic_peek[1] == 'S' && magic_peek[2] == 'N' && magic_peek[3] == 'D') {
            break; // Footer
        }

        file.seekg(pos_before);
        std::uint8_t blk_bytes[kWSndlV2BlockHeaderSize]{};
        file.read(reinterpret_cast<char*>(blk_bytes), sizeof(blk_bytes));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(blk_bytes))) break;

        WSndlV2BlockHeader blk{};
        const auto blk_res = decode_sndl_v2_block_header(blk_bytes, sizeof(blk_bytes), blk);
        if (blk_res.is_failure()) return blk_res;

        std::vector<std::uint8_t> payload(blk.payload_bytes);
        file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(blk.payload_bytes));
        if (file.gcount() < static_cast<std::streamsize>(blk.payload_bytes)) break;

        for (std::uint32_t r = 0; r < blk.record_count; ++r) {
            SndlV2Record rec{};
            const auto rec_res = decode_sndl_v2_record(payload.data() + r * kWSndlV2RecordSize, kWSndlV2RecordSize, rec);
            if (rec_res.is_failure()) return rec_res;

            out_records.push_back(rec);
            if (max_records > 0 && out_records.size() >= max_records) {
                return SndlV2Result::ok();
            }
        }
    }

    return SndlV2Result::ok();
}

std::string SndlV2Validator::format_summary(const SndlV2InspectionInfo& info) {
    std::ostringstream ss;
    ss << "=== SNDL v2 Recording Inspection ===\n";
    ss << "Format Version:            " << info.header.version << "\n";
    ss << "Virtual Core Count:        " << info.header.core_count << "\n";
    ss << "Guest Memory Capacity:     " << (info.header.memory_capacity / (1024 * 1024)) << " MiB\n";
    ss << "Base Snapshot Hash:        0x" << std::hex << info.header.base_snapshot_hash << std::dec << "\n";
    ss << "Initial Global Sequence:   " << info.header.initial_global_sequence << "\n";
    ss << "Total File Size:           " << info.file_size_bytes << " bytes\n";
    ss << "Has Valid Footer:          " << (info.has_valid_footer ? "YES" : "NO (Incomplete)") << "\n";
    if (info.has_valid_footer) {
        ss << "Total Blocks:              " << info.total_blocks << "\n";
        ss << "Footer Total Events:       " << info.total_records << "\n";
        ss << "Max Global Sequence:       " << info.max_global_sequence << "\n";
        ss << "Max Global Retirement:     " << info.max_global_retirement_sequence << "\n";
        ss << "Dropped Events:            " << info.dropped_events << "\n";
        ss << "Completion Status:         " << sndl_v2_completion_status_name(info.footer.completion_status) << "\n";
    }
    return ss.str();
}

SndlV2Result SndlV2Validator::recover_incomplete_recording(
    const std::filesystem::path& input_path,
    const std::filesystem::path& recovered_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed, "Cannot open input file: " + input_path.string());
    }

    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    in.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (in.gcount() < static_cast<std::streamsize>(sizeof(hdr_bytes))) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileTooShort, "File too short for header");
    }

    WSndlV2Header hdr{};
    const auto hdr_res = decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), hdr);
    if (hdr_res.is_failure()) {
        return hdr_res;
    }

    std::ofstream out(recovered_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed, "Cannot open recovered output: " + recovered_path.string());
    }

    // Write placeholder header
    out.write(reinterpret_cast<const char*>(hdr_bytes), sizeof(hdr_bytes));

    std::uint32_t valid_blocks = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t last_seq = hdr.initial_global_sequence;
    std::uint64_t last_ret = 0;

    while (in) {
        const auto pos_before = in.tellg();
        std::uint8_t blk_bytes[kWSndlV2BlockHeaderSize]{};
        in.read(reinterpret_cast<char*>(blk_bytes), sizeof(blk_bytes));
        if (in.gcount() < static_cast<std::streamsize>(sizeof(blk_bytes))) {
            break; // Truncated block header, stop recovery here
        }

        WSndlV2BlockHeader blk{};
        const auto blk_res = decode_sndl_v2_block_header(blk_bytes, sizeof(blk_bytes), blk);
        if (blk_res.is_failure()) {
            break; // Stop at first corrupt block
        }

        std::vector<std::uint8_t> payload(blk.payload_bytes);
        in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(blk.payload_bytes));
        if (in.gcount() < static_cast<std::streamsize>(blk.payload_bytes)) {
            break; // Truncated payload, stop
        }

        const std::uint32_t calc_payload_crc = trace::calculate_crc32c(payload.data(), blk.payload_bytes);
        if (calc_payload_crc != blk.payload_crc32c) {
            break; // Corrupt payload, stop
        }

        // Block is valid! Write to output
        out.write(reinterpret_cast<const char*>(blk_bytes), sizeof(blk_bytes));
        out.write(reinterpret_cast<const char*>(payload.data()), blk.payload_bytes);

        valid_blocks++;
        valid_records += blk.record_count;
        last_seq = blk.last_global_sequence;
    }

    // Synthesize valid footer with Aborted status (never Completed)
    WSndlV2Footer ftr{};
    ftr.magic = kWSndlV2FooterMagic;
    ftr.version = kWSndlVersion2;
    ftr.completion_status = SndlV2CompletionStatus::Aborted;
    ftr.total_blocks = valid_blocks;
    ftr.total_events = valid_records;
    ftr.last_global_sequence = last_seq;
    ftr.last_global_retirement_sequence = last_ret;
    ftr.dropped_events = 0;

    std::uint8_t ftr_bytes[kWSndlV2FooterSize]{};
    encode_sndl_v2_footer(ftr, ftr_bytes);
    out.write(reinterpret_cast<const char*>(ftr_bytes), sizeof(ftr_bytes));

    // Update header totals
    hdr.total_blocks = valid_blocks;
    hdr.total_events = static_cast<std::uint32_t>(valid_records);
    std::uint8_t updated_hdr_bytes[kWSndlV2HeaderSize]{};
    encode_sndl_v2_header(hdr, updated_hdr_bytes);

    out.seekp(0, std::ios::beg);
    out.write(reinterpret_cast<const char*>(updated_hdr_bytes), sizeof(updated_hdr_bytes));
    out.flush();
    out.close();

    return SndlV2Result::ok();
}

} // namespace winios::sndl
