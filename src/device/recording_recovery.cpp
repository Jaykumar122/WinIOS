#include "device/recording_recovery.h"
#include "sndl/sndl_v2_format.h"
#include "trace/trace_serializer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace winios::device {

namespace {

struct IngestedDmaPayload {
    std::uint64_t global_event_sequence{0};
    std::uint64_t descriptor_id{0};
    std::uint64_t dst_paddr{0};
    std::vector<std::uint8_t> data;
    std::uint32_t flags{0};
};

} // namespace

std::string RecoveryReport::format_summary() const {
    std::ostringstream ss;
    ss << "=== Recording Recovery Report ===\n";
    ss << "Success: " << (success ? "YES" : "NO") << "\n";
    if (!success) {
        ss << "Error: " << error_message << "\n";
    }
    ss << "Salvaged Events: " << salvaged_event_count << "\n";
    ss << "Salvaged Blocks: " << salvaged_block_count << "\n";
    ss << "Salvaged DMA Payloads: " << salvaged_payload_count << "\n";
    ss << "Discarded Bytes: " << discarded_bytes << "\n";
    ss << "Last Valid Global Sequence: " << last_valid_global_sequence << "\n";
    ss << "Last Valid Retirement Sequence: " << last_valid_retirement_sequence << "\n";
    ss << "Session ID: 0x" << std::hex << session_id << std::dec << "\n";
    ss << "Base Snapshot Hash: 0x" << std::hex << base_snapshot_hash << std::dec << "\n";
    ss << "Final Status: " << sndl::sndl_v2_completion_status_name(final_completion_status) << "\n";
    ss << "Output SNDL: " << output_sndl_path.string() << "\n";
    if (!output_dma_path.empty()) {
        ss << "Output DMA: " << output_dma_path.string() << "\n";
    }
    return ss.str();
}

RecoveryReport RecordingRecoveryScanner::recover(const RecoveryOptions& options) {
    RecoveryReport report{};
    report.final_completion_status = sndl::SndlV2CompletionStatus::RecoveredIncomplete;

    if (options.output_directory.empty()) {
        report.success = false;
        report.error_message = "Output directory cannot be empty";
        return report;
    }

    if (!std::filesystem::exists(options.input_sndl_path)) {
        report.success = false;
        report.error_message = "Input SNDL file does not exist: " + options.input_sndl_path.string();
        return report;
    }

    std::error_code ec;
    std::filesystem::create_directories(options.output_directory, ec);
    if (ec) {
        report.success = false;
        report.error_message = "Failed to create output directory: " + ec.message();
        return report;
    }

    report.output_sndl_path = options.output_directory / options.input_sndl_path.filename();
    if (std::filesystem::equivalent(report.output_sndl_path, options.input_sndl_path, ec)) {
        report.success = false;
        report.error_message = "Output destination must be an isolated directory, not identical to input";
        return report;
    }

    // --- Step 1: Ingest damaged SNDL file ---
    std::ifstream sndl_file(options.input_sndl_path, std::ios::binary | std::ios::ate);
    if (!sndl_file.is_open()) {
        report.success = false;
        report.error_message = "Failed to open input SNDL file";
        return report;
    }

    const std::uint64_t file_size = static_cast<std::uint64_t>(sndl_file.tellg());
    sndl_file.seekg(0, std::ios::beg);

    if (file_size < sndl::kWSndlV2HeaderSize) {
        report.success = false;
        report.error_message = "Input SNDL file too small to contain header";
        report.discarded_bytes = file_size;
        return report;
    }

    std::uint8_t hdr_bytes[sndl::kWSndlV2HeaderSize];
    sndl_file.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));

    sndl::WSndlV2Header hdr{};
    const auto hdr_dec_res = sndl::decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), hdr);
    if (hdr_dec_res.is_failure()) {
        // Check if magic matches at least
        std::array<std::uint8_t, 8> magic{};
        std::memcpy(magic.data(), hdr_bytes, 8);
        if (magic != sndl::kWSndlV2Magic) {
            report.success = false;
            report.error_message = "Invalid SNDL v2 magic identifier";
            report.discarded_bytes = file_size;
            return report;
        }
    }

    report.base_snapshot_hash = hdr.base_snapshot_hash;
    std::uint64_t session_id = hdr.base_snapshot_hash ? (hdr.base_snapshot_hash ^ 0x534E444C56325345ull) : 0xDEADBEEFCAFE0001ull;

    std::vector<sndl::SndlV2Record> salvaged_records;
    std::uint64_t current_file_offset = sndl::kWSndlV2HeaderSize;
    std::uint64_t last_valid_offset = sndl::kWSndlV2HeaderSize;
    std::uint64_t expected_seq = hdr.initial_global_sequence;

    while (current_file_offset + sndl::kWSndlV2BlockHeaderSize <= file_size) {
        sndl_file.seekg(static_cast<std::streamoff>(current_file_offset), std::ios::beg);

        std::uint8_t blk_bytes[sndl::kWSndlV2BlockHeaderSize];
        sndl_file.read(reinterpret_cast<char*>(blk_bytes), sizeof(blk_bytes));

        sndl::WSndlV2BlockHeader b_hdr{};
        const auto blk_dec_res = sndl::decode_sndl_v2_block_header(blk_bytes, sizeof(blk_bytes), b_hdr);
        if (blk_dec_res.is_failure()) {
            break; // Stop at corrupted/truncated block header
        }

        const std::uint64_t available_bytes = file_size >= (current_file_offset + sndl::kWSndlV2BlockHeaderSize)
            ? (file_size - (current_file_offset + sndl::kWSndlV2BlockHeaderSize))
            : 0;
        const std::uint32_t bytes_to_read = static_cast<std::uint32_t>(std::min<std::uint64_t>(b_hdr.payload_bytes, available_bytes));

        std::vector<std::uint8_t> payload_buf(bytes_to_read);
        sndl_file.read(reinterpret_cast<char*>(payload_buf.data()), payload_buf.size());

        if (bytes_to_read == b_hdr.payload_bytes) {
            const std::uint32_t expected_pay_crc = trace::calculate_crc32c(payload_buf.data(), payload_buf.size());
            if (b_hdr.payload_crc32c != expected_pay_crc) {
                break; // Corrupted payload data
            }
        }

        const std::size_t available_records = bytes_to_read / sndl::kWSndlV2RecordSize;
        if (available_records == 0) {
            break;
        }

        std::vector<sndl::SndlV2Record> block_records;
        block_records.reserve(available_records);

        for (std::size_t r = 0; r < available_records; ++r) {
            sndl::SndlV2Record rec{};
            const auto rec_dec_res = sndl::decode_sndl_v2_record(payload_buf.data() + (r * sndl::kWSndlV2RecordSize),
                                                                 sndl::kWSndlV2RecordSize, rec);
            if (rec_dec_res.is_failure()) {
                break; // Stop at corrupted record
            }

            if (salvaged_records.empty() && r == 0) {
                if (rec.global_event_sequence < hdr.initial_global_sequence) {
                    break;
                }
                expected_seq = rec.global_event_sequence;
            } else {
                if (rec.global_event_sequence != expected_seq) {
                    break;
                }
            }

            if (rec.event_type == sndl::SndlV2EventType::RecordingSessionMetadata) {
                session_id = rec.payload1;
            }

            expected_seq++;
            block_records.push_back(rec);
        }

        if (block_records.empty()) {
            break;
        }

        salvaged_records.insert(salvaged_records.end(), block_records.begin(), block_records.end());
        current_file_offset += sndl::kWSndlV2BlockHeaderSize + (block_records.size() * sndl::kWSndlV2RecordSize);
        last_valid_offset = current_file_offset;

        if (bytes_to_read < b_hdr.payload_bytes || block_records.size() < b_hdr.record_count) {
            break; // Truncation reached
        }
    }

    report.session_id = session_id;

    // --- Step 2: Trim Incomplete Trailing Transactions & Quantums ---
    bool changed = true;
    while (changed && !salvaged_records.empty()) {
        changed = false;

        if (options.trim_incomplete_trailing_dma) {
            std::size_t last_sub_idx = SIZE_MAX;
            bool has_open_sub = false;
            for (std::size_t i = 0; i < salvaged_records.size(); ++i) {
                const auto& rec = salvaged_records[i];
                if (rec.event_type == sndl::SndlV2EventType::DmaSubmissionAccepted) {
                    has_open_sub = true;
                    last_sub_idx = i;
                } else if (rec.event_type == sndl::SndlV2EventType::DmaTransferCompleted ||
                           rec.event_type == sndl::SndlV2EventType::DmaTransferFailed) {
                    has_open_sub = false;
                }
            }
            if (has_open_sub && last_sub_idx != SIZE_MAX) {
                salvaged_records.resize(last_sub_idx);
                changed = true;
                continue;
            }
        }

        // Trim incomplete trailing quantum (CoreSelected or QuantumStart without QuantumEnd)
        std::size_t last_q_idx = SIZE_MAX;
        bool has_open_quantum = false;
        for (std::size_t i = 0; i < salvaged_records.size(); ++i) {
            const auto& rec = salvaged_records[i];
            if (rec.event_type == sndl::SndlV2EventType::CoreSelected ||
                rec.event_type == sndl::SndlV2EventType::QuantumStart) {
                if (!has_open_quantum) {
                    has_open_quantum = true;
                    last_q_idx = i;
                }
            } else if (rec.event_type == sndl::SndlV2EventType::QuantumEnd) {
                has_open_quantum = false;
            }
        }
        if (has_open_quantum && last_q_idx != SIZE_MAX) {
            salvaged_records.resize(last_q_idx);
            changed = true;
            continue;
        }
    }

    // --- Step 3: Scan companion DMA file (if present) ---
    std::filesystem::path dma_in_path = options.input_dma_path;
    if (dma_in_path.empty()) {
        dma_in_path = options.input_sndl_path.string() + ".dma";
    }

    std::vector<IngestedDmaPayload> salvaged_payloads;
    if (std::filesystem::exists(dma_in_path)) {
        std::ifstream dma_file(dma_in_path, std::ios::binary | std::ios::ate);
        if (dma_file.is_open()) {
            const std::uint64_t dma_sz = static_cast<std::uint64_t>(dma_file.tellg());
            dma_file.seekg(0, std::ios::beg);

            if (dma_sz >= 64) {
                std::uint8_t dma_hdr_buf[64];
                dma_file.read(reinterpret_cast<char*>(dma_hdr_buf), 64);
                const std::uint64_t dma_magic = trace::decode_u64_le(dma_hdr_buf);
                if (dma_magic == kDmaDataFileMagic) { // 'WDMA'
                    std::uint64_t dma_offset = 64;
                    while (dma_offset + 48 <= dma_sz) {
                        dma_file.seekg(static_cast<std::streamoff>(dma_offset), std::ios::beg);
                        std::uint8_t rec_hdr[48];
                        dma_file.read(reinterpret_cast<char*>(rec_hdr), 48);

                        const std::uint64_t rec_magic = trace::decode_u64_le(rec_hdr);
                        if (rec_magic != kDmaDataEntryMagic) break;

                        const std::uint32_t expected_hdr_crc = trace::calculate_crc32c(rec_hdr, 44);
                        const std::uint32_t actual_hdr_crc = trace::decode_u32_le(rec_hdr + 44);
                        if (expected_hdr_crc != actual_hdr_crc) break;

                        const std::uint64_t rec_seq = trace::decode_u64_le(rec_hdr + 8);
                        const std::uint64_t rec_desc = trace::decode_u64_le(rec_hdr + 16);
                        const std::uint64_t rec_dst = trace::decode_u64_le(rec_hdr + 24);
                        const std::uint64_t rec_len = trace::decode_u64_le(rec_hdr + 32);
                        const std::uint32_t stored_crc = trace::decode_u32_le(rec_hdr + 40);

                        if (dma_offset + 48 + rec_len > dma_sz) break;

                        std::vector<std::uint8_t> payload_bytes(rec_len);
                        dma_file.read(reinterpret_cast<char*>(payload_bytes.data()), rec_len);

                        const std::uint32_t calc_crc = trace::calculate_crc32c(payload_bytes.data(), payload_bytes.size());
                        if (stored_crc != calc_crc) break;

                        bool seq_in_salvaged = false;
                        for (const auto& sr : salvaged_records) {
                            if (sr.global_event_sequence == rec_seq) {
                                seq_in_salvaged = true;
                                break;
                            }
                        }

                        if (seq_in_salvaged) {
                            IngestedDmaPayload p;
                            p.global_event_sequence = rec_seq;
                            p.descriptor_id = rec_desc;
                            p.dst_paddr = rec_dst;
                            p.data = std::move(payload_bytes);
                            p.flags = 0;
                            salvaged_payloads.push_back(std::move(p));
                        }

                        dma_offset += 48 + rec_len;
                    }
                }
            }
        }
    }

    // --- Step 4: Write salvaged SNDL to isolated destination ---
    std::ofstream out_sndl(report.output_sndl_path, std::ios::binary | std::ios::trunc);
    if (!out_sndl.is_open()) {
        report.success = false;
        report.error_message = "Failed to open output SNDL file: " + report.output_sndl_path.string();
        return report;
    }

    constexpr std::size_t kRecordsPerBlock = 256;
    const std::size_t total_salvaged = salvaged_records.size();
    const std::uint32_t block_count = static_cast<std::uint32_t>((total_salvaged + kRecordsPerBlock - 1) / kRecordsPerBlock);

    sndl::WSndlV2Header out_hdr = hdr;
    out_hdr.total_events = static_cast<std::uint32_t>(total_salvaged);
    out_hdr.total_blocks = block_count;

    std::uint8_t out_hdr_buf[sndl::kWSndlV2HeaderSize]{};
    sndl::encode_sndl_v2_header(out_hdr, out_hdr_buf);
    out_sndl.write(reinterpret_cast<const char*>(out_hdr_buf), sizeof(out_hdr_buf));

    for (std::uint32_t b = 0; b < block_count; ++b) {
        const std::size_t start_idx = b * kRecordsPerBlock;
        const std::size_t count = std::min<std::size_t>(kRecordsPerBlock, total_salvaged - start_idx);

        std::vector<std::uint8_t> payload_chunk(count * sndl::kWSndlV2RecordSize);
        for (std::size_t r = 0; r < count; ++r) {
            sndl::encode_sndl_v2_record(salvaged_records[start_idx + r],
                                        payload_chunk.data() + (r * sndl::kWSndlV2RecordSize));
        }

        sndl::WSndlV2BlockHeader blk{};
        blk.magic = sndl::kWSndlV2BlockMagic;
        blk.version = sndl::kWSndlVersion2;
        blk.flags = 0;
        blk.first_global_sequence = salvaged_records[start_idx].global_event_sequence;
        blk.last_global_sequence = salvaged_records[start_idx + count - 1].global_event_sequence;
        blk.block_index = b;
        blk.record_count = static_cast<std::uint32_t>(count);
        blk.payload_bytes = static_cast<std::uint32_t>(payload_chunk.size());
        blk.payload_crc32c = trace::calculate_crc32c(payload_chunk.data(), payload_chunk.size());

        std::uint8_t blk_hdr_buf[sndl::kWSndlV2BlockHeaderSize]{};
        sndl::encode_sndl_v2_block_header(blk, blk_hdr_buf);

        out_sndl.write(reinterpret_cast<const char*>(blk_hdr_buf), sizeof(blk_hdr_buf));
        out_sndl.write(reinterpret_cast<const char*>(payload_chunk.data()), payload_chunk.size());
    }

    // Write recovered footer
    sndl::WSndlV2Footer out_ftr{};
    out_ftr.magic = sndl::kWSndlV2FooterMagic;
    out_ftr.version = sndl::kWSndlVersion2;
    out_ftr.completion_status = sndl::SndlV2CompletionStatus::RecoveredIncomplete;
    out_ftr.total_events = static_cast<std::uint64_t>(total_salvaged);
    out_ftr.total_blocks = block_count;
    out_ftr.last_global_sequence = total_salvaged > 0 ? salvaged_records.back().global_event_sequence : 0;
    out_ftr.last_global_retirement_sequence = total_salvaged > 0 ? salvaged_records.back().global_retirement_sequence : 0;
    out_ftr.dropped_events = 0;

    std::uint8_t ftr_buf[sndl::kWSndlV2FooterSize]{};
    sndl::encode_sndl_v2_footer(out_ftr, ftr_buf);
    out_sndl.write(reinterpret_cast<const char*>(ftr_buf), sizeof(ftr_buf));
    out_sndl.close();

    // --- Step 5: Write salvaged companion DMA store (if payloads exist or input had DMA file) ---
    if (!salvaged_payloads.empty() || std::filesystem::exists(dma_in_path)) {
        report.output_dma_path = report.output_sndl_path.string() + ".dma";
        DmaPayloadStore out_dma_store;
        const auto dma_open_res = out_dma_store.open_write(report.output_dma_path, session_id, hdr.base_snapshot_hash);
        if (dma_open_res.is_success()) {
            for (const auto& p : salvaged_payloads) {
                (void)out_dma_store.record_payload(p.global_event_sequence, p.descriptor_id, p.dst_paddr, p.data.data(), p.data.size());
            }
            (void)out_dma_store.finalize(RecordingLifecycleState::RecoveredIncomplete);
        }
    }

    report.success = true;
    report.salvaged_event_count = total_salvaged;
    report.salvaged_block_count = block_count;
    report.salvaged_payload_count = salvaged_payloads.size();
    report.discarded_bytes = file_size >= last_valid_offset ? (file_size - last_valid_offset) : 0;
    report.last_valid_global_sequence = total_salvaged > 0 ? salvaged_records.back().global_event_sequence : 0;
    report.last_valid_retirement_sequence = total_salvaged > 0 ? salvaged_records.back().global_retirement_sequence : 0;

    return report;
}

} // namespace winios::device
