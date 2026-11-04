#include "replay/smp_replay_reader.h"
#include "sndl/sndl_v2_format.h"
#include "trace/trace_serializer.h"
#include "trace/trace_types.h"

#include <iostream>
#include <system_error>

namespace winios::replay {

SndlV2ReplayReader::SndlV2ReplayReader() = default;

SndlV2ReplayReader::~SndlV2ReplayReader() {
    close();
}

void SndlV2ReplayReader::close() noexcept {
    if (file_.is_open()) {
        file_.close();
    }
    is_open_ = false;
    has_footer_ = false;
    header_ = {};
    footer_ = {};
    path_.clear();
    buffer_.clear();
    buffer_index_ = 0;
    file_size_ = 0;
    current_file_offset_ = 0;
    current_block_index_ = 0;
    records_consumed_ = 0;
    last_seen_sequence_ = 0;
}

sndl::SndlV2Result SndlV2ReplayReader::open(const std::filesystem::path& path) {
    close();
    path_ = path;

    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::FileOpenFailed, "Cannot access file: " + path.string());
    }
    file_size_ = static_cast<std::uint64_t>(sz);

    if (file_size_ < sndl::kWSndlV2HeaderSize) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::FileTooShort, "File is smaller than SNDL v2 header size");
    }

    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::FileOpenFailed, "Could not open file: " + path.string());
    }

    // 1. Read and decode 64-byte Header
    std::uint8_t hdr_bytes[sndl::kWSndlV2HeaderSize];
    file_.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (file_.gcount() < static_cast<std::streamsize>(sizeof(hdr_bytes))) {
        close();
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::FileReadFailed, "Failed to read SNDL v2 header");
    }

    const auto hdr_res = sndl::decode_sndl_v2_header(hdr_bytes, sizeof(hdr_bytes), header_);
    if (hdr_res.is_failure()) {
        close();
        return hdr_res;
    }

    // 2. Read and decode 64-byte Footer if file is large enough
    if (file_size_ >= sndl::kWSndlV2HeaderSize + sndl::kWSndlV2FooterSize) {
        file_.seekg(static_cast<std::streamoff>(file_size_ - sndl::kWSndlV2FooterSize), std::ios::beg);
        std::uint8_t ftr_bytes[sndl::kWSndlV2FooterSize];
        file_.read(reinterpret_cast<char*>(ftr_bytes), sizeof(ftr_bytes));
        if (file_.gcount() == static_cast<std::streamsize>(sizeof(ftr_bytes))) {
            const auto ftr_res = sndl::decode_sndl_v2_footer(ftr_bytes, sizeof(ftr_bytes), footer_);
            if (ftr_res.is_success()) {
                has_footer_ = true;
            }
        }
    }

    // Reposition to start of first block
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(sndl::kWSndlV2HeaderSize), std::ios::beg);
    current_file_offset_ = sndl::kWSndlV2HeaderSize;
    current_block_index_ = 0;
    records_consumed_ = 0;
    last_seen_sequence_ = 0;
    is_open_ = true;

    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SndlV2ReplayReader::buffer_next_block() {
    if (!is_open_) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::FileReadFailed, "Reader is not open");
    }

    buffer_.clear();
    buffer_index_ = 0;

    const std::uint64_t limit_offset = has_footer_ ? (file_size_ - sndl::kWSndlV2FooterSize) : file_size_;
    if (current_file_offset_ >= limit_offset) {
        // No more blocks in file
        return sndl::SndlV2Result::ok();
    }

    if (current_file_offset_ + sndl::kWSndlV2BlockHeaderSize > limit_offset) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::TruncatedBlock, "Truncated block header at offset " + std::to_string(current_file_offset_));
    }

    // Read 48-byte block header
    file_.seekg(static_cast<std::streamoff>(current_file_offset_), std::ios::beg);
    std::uint8_t blk_hdr_bytes[sndl::kWSndlV2BlockHeaderSize];
    file_.read(reinterpret_cast<char*>(blk_hdr_bytes), sizeof(blk_hdr_bytes));
    if (file_.gcount() < static_cast<std::streamsize>(sizeof(blk_hdr_bytes))) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::TruncatedBlock, "Failed to read block header");
    }

    sndl::WSndlV2BlockHeader blk{};
    const auto blk_res = sndl::decode_sndl_v2_block_header(blk_hdr_bytes, sizeof(blk_hdr_bytes), blk);
    if (blk_res.is_failure()) {
        return blk_res;
    }

    const std::size_t payload_bytes = static_cast<std::size_t>(blk.payload_bytes);
    if (current_file_offset_ + sndl::kWSndlV2BlockHeaderSize + payload_bytes > limit_offset) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::TruncatedBlock, "Truncated block payload");
    }

    std::vector<std::uint8_t> payload(payload_bytes);
    file_.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload_bytes));
    if (file_.gcount() < static_cast<std::streamsize>(payload_bytes)) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::TruncatedBlock, "Failed to read block payload");
    }

    const std::uint32_t calc_crc = trace::calculate_crc32c(payload.data(), payload_bytes);
    if (calc_crc != blk.payload_crc32c) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::BlockPayloadCrcMismatch, "Block payload CRC32C mismatch");
    }

    // Decode records in block
    buffer_.reserve(blk.record_count);
    for (std::uint32_t r = 0; r < blk.record_count; ++r) {
        sndl::SndlV2Record rec{};
        const auto rec_res = sndl::decode_sndl_v2_record(payload.data() + r * sndl::kWSndlV2RecordSize, sndl::kWSndlV2RecordSize, rec);
        if (rec_res.is_failure()) {
            return rec_res;
        }

        // Monotonicity check
        if (records_consumed_ == 0 && buffer_.empty()) {
            if (rec.global_event_sequence < header_.initial_global_sequence) {
                return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::InvalidSequence, "First event sequence is less than initial sequence");
            }
        } else {
            if (rec.global_event_sequence <= last_seen_sequence_) {
                return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::InvalidSequence, "Event sequence monotonicity violation");
            }
        }
        last_seen_sequence_ = rec.global_event_sequence;
        buffer_.push_back(rec);
    }

    current_file_offset_ += sndl::kWSndlV2BlockHeaderSize + payload_bytes;
    current_block_index_++;
    return sndl::SndlV2Result::ok();
}

bool SndlV2ReplayReader::has_next() {
    if (!is_open_) return false;
    if (buffer_index_ < buffer_.size()) return true;

    const auto res = buffer_next_block();
    if (res.is_failure()) {
        return false;
    }
    return buffer_index_ < buffer_.size();
}

sndl::SndlV2Result SndlV2ReplayReader::peek_next(sndl::SndlV2Record& out_record) {
    if (!has_next()) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::IncompleteRecording, "No more records in stream");
    }
    out_record = buffer_[buffer_index_];
    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SndlV2ReplayReader::read_next(sndl::SndlV2Record& out_record) {
    if (!has_next()) {
        return sndl::SndlV2Result::fail(sndl::SndlV2ErrorCode::IncompleteRecording, "No more records in stream");
    }
    out_record = buffer_[buffer_index_++];
    records_consumed_++;
    return sndl::SndlV2Result::ok();
}

} // namespace winios::replay
