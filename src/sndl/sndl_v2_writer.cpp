#include "sndl/sndl_v2_writer.h"
#include "trace/trace_serializer.h"

#include <algorithm>

namespace winios::sndl {

SndlV2Writer::SndlV2Writer() : SndlV2Writer(SndlV2WriterConfig{}) {}

SndlV2Writer::SndlV2Writer(SndlV2WriterConfig config)
    : config_(config) {
    if (config_.records_per_block == 0) {
        config_.records_per_block = kDefaultV2RecordsPerBlock;
    }
    block_payload_buffer_.resize(config_.records_per_block * kWSndlV2RecordSize);
}

SndlV2Writer::~SndlV2Writer() {
    close();
}

SndlV2Writer::SndlV2Writer(SndlV2Writer&& other) noexcept
    : config_(other.config_),
      file_path_(std::move(other.file_path_)),
      file_(std::move(other.file_)),
      header_(other.header_),
      block_payload_buffer_(std::move(other.block_payload_buffer_)),
      block_records_count_(other.block_records_count_),
      block_first_seq_(other.block_first_seq_),
      block_last_seq_(other.block_last_seq_),
      block_index_(other.block_index_),
      total_events_written_(other.total_events_written_),
      first_global_seq_(other.first_global_seq_),
      last_global_seq_(other.last_global_seq_),
      last_global_retirement_seq_(other.last_global_retirement_seq_),
      dropped_events_(other.dropped_events_),
      overflow_(other.overflow_),
      finalized_(other.finalized_) {
    other.close();
}

SndlV2Writer& SndlV2Writer::operator=(SndlV2Writer&& other) noexcept {
    if (this != &other) {
        close();
        config_ = other.config_;
        file_path_ = std::move(other.file_path_);
        file_ = std::move(other.file_);
        header_ = other.header_;
        block_payload_buffer_ = std::move(other.block_payload_buffer_);
        block_records_count_ = other.block_records_count_;
        block_first_seq_ = other.block_first_seq_;
        block_last_seq_ = other.block_last_seq_;
        block_index_ = other.block_index_;
        total_events_written_ = other.total_events_written_;
        first_global_seq_ = other.first_global_seq_;
        last_global_seq_ = other.last_global_seq_;
        last_global_retirement_seq_ = other.last_global_retirement_seq_;
        dropped_events_ = other.dropped_events_;
        overflow_ = other.overflow_;
        finalized_ = other.finalized_;
        other.close();
    }
    return *this;
}

SndlV2Result SndlV2Writer::open(const std::filesystem::path& path, const WSndlV2Header& header) {
    close();
    file_path_ = path;
    header_ = header;
    header_.magic = kWSndlV2Magic;
    header_.version = kWSndlVersion2;
    header_.total_events = 0;
    header_.total_blocks = 0;

    file_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileOpenFailed,
                                  "Failed to open SNDL v2 file for writing: " + file_path_.string());
    }

    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    encode_sndl_v2_header(header_, hdr_bytes);
    file_.write(reinterpret_cast<const char*>(hdr_bytes), sizeof(hdr_bytes));
    if (!file_) {
        close();
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed,
                                  "Failed to write SNDL v2 header: " + file_path_.string());
    }

    block_index_ = 0;
    block_records_count_ = 0;
    block_first_seq_ = 0;
    block_last_seq_ = 0;
    total_events_written_ = 0;
    first_global_seq_ = 0;
    last_global_seq_ = 0;
    last_global_retirement_seq_ = 0;
    dropped_events_ = 0;
    overflow_ = false;
    finalized_ = false;

    return SndlV2Result::ok();
}

SndlV2Result SndlV2Writer::write_record(const SndlV2Record& record) {
    if (!file_.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed, "Writer is not open");
    }

    if (overflow_) {
        dropped_events_++;
        return SndlV2Result::ok();
    }

    if (total_events_written_ + block_records_count_ >= config_.max_buffer_records) {
        overflow_ = true;
        dropped_events_++;
        return SndlV2Result::fail(SndlV2ErrorCode::BufferOverflow, "Writer buffer capacity exceeded");
    }

    if (block_records_count_ == 0) {
        block_first_seq_ = record.global_event_sequence;
    }
    block_last_seq_ = record.global_event_sequence;

    if (first_global_seq_ == 0) {
        first_global_seq_ = record.global_event_sequence;
    }
    last_global_seq_ = record.global_event_sequence;
    last_global_retirement_seq_ = std::max(last_global_retirement_seq_, record.global_retirement_sequence);

    // Direct encoding into preallocated payload buffer
    const std::size_t offset = static_cast<std::size_t>(block_records_count_) * kWSndlV2RecordSize;
    if (offset + kWSndlV2RecordSize > block_payload_buffer_.size()) {
        block_payload_buffer_.resize(offset + kWSndlV2RecordSize * 2);
    }
    encode_sndl_v2_record(record, block_payload_buffer_.data() + offset);
    block_records_count_++;

    if (block_records_count_ >= config_.records_per_block) {
        return flush_current_block();
    }

    return SndlV2Result::ok();
}

SndlV2Result SndlV2Writer::flush_current_block() {
    if (block_records_count_ == 0 || !file_.is_open()) {
        return SndlV2Result::ok();
    }

    const std::uint32_t count = block_records_count_;
    const std::uint32_t payload_bytes = count * static_cast<std::uint32_t>(kWSndlV2RecordSize);

    const std::uint32_t payload_crc = trace::calculate_crc32c(block_payload_buffer_.data(), payload_bytes);

    WSndlV2BlockHeader blk{};
    blk.magic = kWSndlV2BlockMagic;
    blk.version = kWSndlVersion2;
    blk.flags = 0;
    blk.block_index = block_index_;
    blk.record_count = count;
    blk.payload_bytes = payload_bytes;
    blk.first_global_sequence = block_first_seq_;
    blk.last_global_sequence = block_last_seq_;
    blk.payload_crc32c = payload_crc;

    std::uint8_t blk_bytes[kWSndlV2BlockHeaderSize]{};
    encode_sndl_v2_block_header(blk, blk_bytes);

    file_.write(reinterpret_cast<const char*>(blk_bytes), sizeof(blk_bytes));
    file_.write(reinterpret_cast<const char*>(block_payload_buffer_.data()), payload_bytes);

    if (!file_) {
        overflow_ = true;
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed, "Failed to write block to disk");
    }

    block_index_++;
    total_events_written_ += count;
    block_records_count_ = 0;
    block_first_seq_ = 0;
    block_last_seq_ = 0;

    return SndlV2Result::ok();
}

SndlV2Result SndlV2Writer::finalize(SndlV2CompletionStatus status) {
    if (!file_.is_open()) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed, "Writer is not open");
    }
    if (finalized_) {
        return SndlV2Result::ok();
    }

    if (block_records_count_ > 0) {
        const auto flush_res = flush_current_block();
        if (flush_res.is_failure()) {
            return flush_res;
        }
    }

    if (overflow_ && status == SndlV2CompletionStatus::Completed) {
        status = SndlV2CompletionStatus::Overflow;
    }

    WSndlV2Footer ftr{};
    ftr.magic = kWSndlV2FooterMagic;
    ftr.version = kWSndlVersion2;
    ftr.completion_status = status;
    ftr.total_blocks = block_index_;
    ftr.total_events = total_events_written_;
    ftr.last_global_sequence = last_global_seq_;
    ftr.last_global_retirement_sequence = last_global_retirement_seq_;
    ftr.dropped_events = dropped_events_;

    std::uint8_t ftr_bytes[kWSndlV2FooterSize]{};
    encode_sndl_v2_footer(ftr, ftr_bytes);
    file_.write(reinterpret_cast<const char*>(ftr_bytes), sizeof(ftr_bytes));

    if (!file_) {
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed, "Failed to write footer");
    }

    // Rewrite header with updated totals
    header_.total_events = static_cast<std::uint32_t>(total_events_written_);
    header_.total_blocks = block_index_;
    std::uint8_t hdr_bytes[kWSndlV2HeaderSize]{};
    encode_sndl_v2_header(header_, hdr_bytes);

    file_.seekp(0, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(hdr_bytes), sizeof(hdr_bytes));
    file_.flush();
    file_.close();

    finalized_ = true;
    return SndlV2Result::ok();
}

void SndlV2Writer::close() noexcept {
    if (file_.is_open()) {
        file_.close();
    }
    block_records_count_ = 0;
    block_first_seq_ = 0;
    block_last_seq_ = 0;
    block_index_ = 0;
    total_events_written_ = 0;
    finalized_ = false;
}

} // namespace winios::sndl
