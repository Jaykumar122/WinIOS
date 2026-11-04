#include "sndl/sndl_validation.h"
#include "sndl/sndl_event_encoder.h"
#include "snapshot/wsnap_format.h"
#include "trace/trace_serializer.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace winios::sndl {

SndlValidationResult SndlValidation::validate_buffer(
    const std::uint8_t* data, std::size_t size,
    std::uint64_t expected_snapshot_hash) {
    SndlValidationResult res{};
    res.is_valid = false;

    if (size < kWSndlHeaderSize + kWSndlFooterSize) {
        res.code = SndlErrorCode::FileTooShort;
        res.message = "Buffer size (" + std::to_string(size) + " B) smaller than minimum SNDL file size";
        return res;
    }

    // 1. Decode and validate header
    WSndlHeader hdr{};
    const auto hdr_res = decode_sndl_header(data, size, hdr);
    if (hdr_res.is_failure()) {
        res.code = hdr_res.code;
        res.message = hdr_res.message;
        return res;
    }

    res.core_count = hdr.core_count;
    res.base_snapshot_hash = hdr.base_snapshot_hash;

    // Optional snapshot hash verification
    if (expected_snapshot_hash != 0) {
        res.snapshot_checked = true;
        if (hdr.base_snapshot_hash != expected_snapshot_hash) {
            res.code = SndlErrorCode::SnapshotMismatch;
            res.message = "SNDL base snapshot hash mismatch: expected 0x" +
                          std::to_string(expected_snapshot_hash) + ", found 0x" +
                          std::to_string(hdr.base_snapshot_hash);
            res.snapshot_matched = false;
            return res;
        }
        res.snapshot_matched = true;
    }

    // 2. Scan through blocks
    std::size_t offset = kWSndlHeaderSize;
    std::uint64_t last_seq = 0;
    std::uint64_t accumulated_records = 0;
    std::uint32_t accumulated_blocks = 0;
    bool first_record = true;

    while (offset + kWSndlFooterSize <= size) {
        // Check if we hit the footer magic
        if (offset + kWSndlFooterSize == size &&
            std::memcmp(data + offset, kWSndlFooterMagic.data(), 8) == 0) {
            break;
        }

        WSndlBlockHeader blk{};
        const auto blk_res = decode_sndl_block_header(data + offset, size - offset, blk);
        if (blk_res.is_failure()) {
            res.code = blk_res.code;
            res.message = "At offset 0x" + std::to_string(offset) + ": " + blk_res.message;
            return res;
        }

        offset += kWSndlBlockHeaderSize;

        if (offset + blk.payload_bytes > size) {
            res.code = SndlErrorCode::TruncatedBlock;
            res.message = "Block payload truncated: requires " + std::to_string(blk.payload_bytes) +
                          " B, but only " + std::to_string(size - offset) + " B remaining";
            return res;
        }

        // Validate payload CRC32C
        const std::uint32_t computed_crc = trace::calculate_crc32c(data + offset, blk.payload_bytes);
        if (computed_crc != blk.payload_crc32c) {
            res.code = SndlErrorCode::BlockPayloadCrcMismatch;
            res.message = "Block payload CRC32C mismatch in block " + std::to_string(accumulated_blocks);
            return res;
        }

        // Validate each record in this block
        const std::size_t rec_count = blk.record_count;
        for (std::size_t r = 0; r < rec_count; ++r) {
            SndlRecord rec{};
            const auto rec_res = decode_record(data + offset + r * kWSndlRecordDiskSize, kWSndlRecordDiskSize, rec);
            if (rec_res.is_failure()) {
                res.code = rec_res.code;
                res.message = "Record decode error in block " + std::to_string(accumulated_blocks) +
                              ", index " + std::to_string(r);
                return res;
            }

            if (first_record) {
                res.first_sequence = rec.sequence_num;
                first_record = false;
            } else {
                if (rec.sequence_num <= last_seq) {
                    res.code = SndlErrorCode::InvalidSequence;
                    res.message = "Sequence monotonicity violated: seq " + std::to_string(rec.sequence_num) +
                                  " <= prev " + std::to_string(last_seq);
                    return res;
                }
            }
            last_seq = rec.sequence_num;
            ++accumulated_records;
        }

        offset += blk.payload_bytes;
        ++accumulated_blocks;
    }

    // 3. Decode and validate footer
    if (offset + kWSndlFooterSize > size) {
        res.code = SndlErrorCode::TruncatedBlock;
        res.message = "Missing or truncated footer at offset 0x" + std::to_string(offset);
        return res;
    }

    WSndlFooter ftr{};
    const auto ftr_res = decode_sndl_footer(data + offset, size - offset, ftr);
    if (ftr_res.is_failure()) {
        res.code = ftr_res.code;
        res.message = ftr_res.message;
        return res;
    }

    res.total_records = accumulated_records;
    res.total_blocks = accumulated_blocks;
    res.last_sequence = last_seq;
    res.total_instructions = ftr.total_instructions;
    res.finalization_status = ftr.finalization_status;

    if (ftr.finalization_status == SndlFinalizationStatus::Overflow) {
        res.code = SndlErrorCode::BufferOverflow;
        res.message = "Recording was terminated due to buffer overflow";
        return res;
    }
    if (ftr.finalization_status != SndlFinalizationStatus::Completed) {
        res.code = SndlErrorCode::IncompleteRecording;
        res.message = "Recording was not cleanly completed (status=" +
                      std::to_string(static_cast<int>(ftr.finalization_status)) + ")";
        return res;
    }

    if (ftr.total_records != accumulated_records) {
        res.code = SndlErrorCode::MalformedRecord;
        res.message = "Footer total_records (" + std::to_string(ftr.total_records) +
                      ") does not match accumulated records (" + std::to_string(accumulated_records) + ")";
        return res;
    }

    res.is_valid = true;
    res.code = SndlErrorCode::Success;
    res.message = "SNDL file is structurally valid and complete";
    return res;
}

SndlValidationResult SndlValidation::validate_file(
    const std::filesystem::path& file_path,
    const std::optional<std::filesystem::path>& snapshot_path) {
    SndlValidationResult res{};
    res.is_valid = false;

    if (!std::filesystem::exists(file_path)) {
        res.code = SndlErrorCode::FileOpenFailed;
        res.message = "SNDL file not found: " + file_path.string();
        return res;
    }

    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        res.code = SndlErrorCode::FileOpenFailed;
        res.message = "Could not open SNDL file: " + file_path.string();
        return res;
    }

    const auto file_size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!file) {
        res.code = SndlErrorCode::FileReadFailed;
        res.message = "Failed reading SNDL file bytes: " + file_path.string();
        return res;
    }

    std::uint64_t expected_hash = 0;
    if (snapshot_path.has_value()) {
        if (!std::filesystem::exists(*snapshot_path)) {
            res.code = SndlErrorCode::SnapshotNotFound;
            res.message = "Associated snapshot file not found: " + snapshot_path->string();
            return res;
        }

        // Read snapshot header
        std::ifstream snap_file(*snapshot_path, std::ios::binary);
        if (!snap_file.is_open()) {
            res.code = SndlErrorCode::SnapshotNotFound;
            res.message = "Failed opening associated snapshot: " + snapshot_path->string();
            return res;
        }

        std::uint8_t snap_hdr_bytes[64]{};
        snap_file.read(reinterpret_cast<char*>(snap_hdr_bytes), 64);
        if (snap_file.gcount() == 64) {
            snapshot::WSnapHeader snap_hdr{};
            if (snapshot::decode_wsnap_header(snap_hdr_bytes, 64, snap_hdr).success) {
                expected_hash = snap_hdr.header_crc32c;
            }
        }
    }

    return validate_buffer(buffer.data(), file_size, expected_hash);
}

SndlResult SndlValidation::inspect_file(
    const std::filesystem::path& file_path,
    SndlInspectionInfo& out_info) {
    if (!std::filesystem::exists(file_path)) {
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "File not found: " + file_path.string());
    }

    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "Could not open file: " + file_path.string());
    }

    const auto file_size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!file) {
        return SndlResult::fail(SndlErrorCode::FileReadFailed, "Failed reading file data");
    }

    out_info = SndlInspectionInfo{};
    out_info.file_size = file_size;

    if (file_size < kWSndlHeaderSize) {
        return SndlResult::fail(SndlErrorCode::FileTooShort, "File smaller than header");
    }

    const auto hdr_res = decode_sndl_header(buffer.data(), file_size, out_info.header);
    if (hdr_res.is_failure()) {
        return hdr_res;
    }

    std::size_t offset = kWSndlHeaderSize;
    while (offset + kWSndlFooterSize <= file_size) {
        if (offset + kWSndlFooterSize == file_size &&
            std::memcmp(buffer.data() + offset, kWSndlFooterMagic.data(), 8) == 0) {
            break;
        }

        WSndlBlockHeader blk{};
        const auto blk_res = decode_sndl_block_header(buffer.data() + offset, file_size - offset, blk);
        if (blk_res.is_failure()) {
            break; // Stop block parsing on corrupted block
        }

        out_info.blocks.push_back(blk);
        offset += kWSndlBlockHeaderSize;

        // Sample records and count event types
        const std::size_t rec_count = blk.record_count;
        for (std::size_t r = 0; r < rec_count; ++r) {
            SndlRecord rec{};
            if (decode_record(buffer.data() + offset + r * kWSndlRecordDiskSize, kWSndlRecordDiskSize, rec).is_success()) {
                const auto t = static_cast<std::size_t>(rec.event_type);
                if (t < 16) {
                    out_info.event_type_counts[t]++;
                }
                if (out_info.sample_records.size() < 10) {
                    out_info.sample_records.push_back(rec);
                }
            }
        }
        offset += blk.payload_bytes;
    }

    if (offset + kWSndlFooterSize <= file_size) {
        if (decode_sndl_footer(buffer.data() + offset, file_size - offset, out_info.footer).is_success()) {
            out_info.footer_present = true;
            out_info.cleanly_finalized = (out_info.footer.finalization_status == SndlFinalizationStatus::Completed);
        }
    }

    return SndlResult::ok();
}

std::string SndlValidation::format_summary(const SndlInspectionInfo& info) {
    std::ostringstream ss;
    ss << "=== WinIOS Sparse Non-Deterministic Log (SNDL) Inspection ===\n";
    ss << "File Size:            " << info.file_size << " bytes\n";
    ss << "Format Version:       " << info.header.version << "\n";
    ss << "Core Count:           " << info.header.core_count << "\n";
    ss << "Base Snapshot Hash:   0x" << std::hex << info.header.base_snapshot_hash << std::dec << "\n";
    ss << "Header CRC32C:        0x" << std::hex << info.header.header_crc32c << std::dec << "\n";
    ss << "Total Blocks:         " << info.blocks.size() << "\n";
    ss << "Cleanly Finalized:    " << (info.cleanly_finalized ? "YES" : "NO") << "\n";

    if (info.footer_present) {
        ss << "Final Record Count:   " << info.footer.total_records << "\n";
        ss << "Final Sequence Num:   " << info.footer.last_sequence << "\n";
        ss << "Total Instructions:   " << info.footer.total_instructions << "\n";
        ss << "Final Status:         ";
        switch (info.footer.finalization_status) {
        case SndlFinalizationStatus::Completed:  ss << "Completed (Clean)"; break;
        case SndlFinalizationStatus::Incomplete: ss << "Incomplete"; break;
        case SndlFinalizationStatus::Aborted:    ss << "Aborted"; break;
        case SndlFinalizationStatus::Overflow:   ss << "Buffer Overflow (Failed)"; break;
        }
        ss << "\n";
    }

    ss << "\n--- Nondeterministic Event Taxonomy ---\n";
    for (std::size_t t = 1; t <= 9; ++t) {
        const auto type = static_cast<SndlEventType>(t);
        ss << "  [" << std::setw(2) << t << "] "
           << std::setw(28) << std::left << sndl_event_type_name(type) << " : "
           << info.event_type_counts[t] << "\n";
    }

    if (!info.sample_records.empty()) {
        ss << "\n--- First " << info.sample_records.size() << " Sample Records ---\n";
        for (const auto& r : info.sample_records) {
            ss << "  [Seq " << std::setw(4) << r.sequence_num << "] Core " << r.core_id
               << " | RetCnt " << std::setw(6) << r.instruction_retirement_count
               << " (Sub " << r.sub_instruction_ordinal << ")"
               << " | PC 0x" << std::hex << r.pc << std::dec
               << " | " << sndl_event_type_name(r.event_type)
               << " (" << sndl_boundary_type_name(r.boundary_type) << ")"
               << " | P1: 0x" << std::hex << r.payload1
               << " P2: 0x" << r.payload2 << std::dec << "\n";
        }
    }

    return ss.str();
}

} // namespace winios::sndl
