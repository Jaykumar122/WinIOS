#include "sndl/sndl_writer.h"
#include "sndl/sndl_event_encoder.h"
#include "trace/trace_serializer.h"

#include <cstring>
#include <system_error>

namespace winios::sndl {

SndlWriter::SndlWriter(std::uint32_t max_records_per_block)
    : max_records_per_block_(max_records_per_block > 0 ? max_records_per_block : kDefaultMaxRecordsPerBlock) {
    block_records_.reserve(max_records_per_block_);
    block_payload_buffer_.resize(max_records_per_block_ * kWSndlRecordDiskSize);
}

SndlWriter::~SndlWriter() {
    close();
}

SndlWriter::SndlWriter(SndlWriter&& other) noexcept
    : max_records_per_block_(other.max_records_per_block_),
      file_path_(std::move(other.file_path_)),
      file_(std::move(other.file_)),
      header_(other.header_),
      block_records_(std::move(other.block_records_)),
      block_payload_buffer_(std::move(other.block_payload_buffer_)),
      total_records_(other.total_records_),
      block_count_(other.block_count_),
      first_sequence_(other.first_sequence_),
      last_sequence_(other.last_sequence_),
      failed_(other.failed_),
      finalized_(other.finalized_) {
    other.failed_ = true;
}

SndlWriter& SndlWriter::operator=(SndlWriter&& other) noexcept {
    if (this != &other) {
        close();
        max_records_per_block_ = other.max_records_per_block_;
        file_path_ = std::move(other.file_path_);
        file_ = std::move(other.file_);
        header_ = other.header_;
        block_records_ = std::move(other.block_records_);
        block_payload_buffer_ = std::move(other.block_payload_buffer_);
        total_records_ = other.total_records_;
        block_count_ = other.block_count_;
        first_sequence_ = other.first_sequence_;
        last_sequence_ = other.last_sequence_;
        failed_ = other.failed_;
        finalized_ = other.finalized_;
        other.failed_ = true;
    }
    return *this;
}

SndlResult SndlWriter::open(const std::filesystem::path& file_path, const WSndlHeader& initial_header) {
    close();

    file_path_ = file_path;
    header_ = initial_header;
    total_records_ = 0;
    block_count_ = 0;
    first_sequence_ = 0;
    last_sequence_ = 0;
    failed_ = false;
    finalized_ = false;
    block_records_.clear();

    file_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        failed_ = true;
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "Failed to open output file: " + file_path_.string());
    }

    // Write initial header placeholder
    std::uint8_t hdr_bytes[kWSndlHeaderSize]{};
    encode_sndl_header(header_, hdr_bytes);
    file_.write(reinterpret_cast<const char*>(hdr_bytes), kWSndlHeaderSize);
    if (!file_) {
        failed_ = true;
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Failed to write SNDL header");
    }

    return SndlResult::ok();
}

SndlResult SndlWriter::write_record(const SndlRecord& record) {
    if (failed_) {
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Cannot write to failed SNDL writer");
    }
    if (!file_.is_open()) {
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "SNDL writer is not open");
    }

    if (total_records_ == 0 && block_records_.empty()) {
        first_sequence_ = record.sequence_num;
    }
    last_sequence_ = record.sequence_num;

    block_records_.push_back(record);
    if (block_records_.size() >= max_records_per_block_) {
        return flush();
    }

    return SndlResult::ok();
}

SndlResult SndlWriter::flush() {
    if (failed_) {
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Cannot flush failed SNDL writer");
    }
    if (!file_.is_open()) {
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "SNDL writer is not open");
    }

    if (block_records_.empty()) {
        return SndlResult::ok();
    }

    return write_block_locked();
}

SndlResult SndlWriter::write_block_locked() {
    const std::uint32_t count = static_cast<std::uint32_t>(block_records_.size());
    const std::uint32_t payload_bytes = count * static_cast<std::uint32_t>(kWSndlRecordDiskSize);

    if (block_payload_buffer_.size() < payload_bytes) {
        block_payload_buffer_.resize(payload_bytes);
    }

    for (std::size_t i = 0; i < count; ++i) {
        encode_record(block_records_[i], block_payload_buffer_.data() + i * kWSndlRecordDiskSize);
    }

    const std::uint32_t payload_crc = trace::calculate_crc32c(block_payload_buffer_.data(), payload_bytes);

    WSndlBlockHeader blk{};
    blk.magic = kWSndlBlockMagic;
    blk.version = 1;
    blk.flags = 0;
    blk.record_count = count;
    blk.payload_bytes = payload_bytes;
    blk.first_sequence = block_records_.front().sequence_num;
    blk.last_sequence = block_records_.back().sequence_num;
    blk.payload_crc32c = payload_crc;

    std::uint8_t blk_hdr_bytes[kWSndlBlockHeaderSize]{};
    encode_sndl_block_header(blk, blk_hdr_bytes);

    file_.write(reinterpret_cast<const char*>(blk_hdr_bytes), kWSndlBlockHeaderSize);
    if (!file_) {
        failed_ = true;
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Failed to write block header");
    }

    file_.write(reinterpret_cast<const char*>(block_payload_buffer_.data()), payload_bytes);
    if (!file_) {
        failed_ = true;
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Failed to write block payload");
    }

    total_records_ += count;
    ++block_count_;
    block_records_.clear();

    return SndlResult::ok();
}

SndlResult SndlWriter::finalize(SndlFinalizationStatus status, std::uint64_t total_instructions) {
    if (finalized_) {
        return SndlResult::ok();
    }
    if (!file_.is_open()) {
        return SndlResult::fail(SndlErrorCode::FileOpenFailed, "Writer is not open");
    }

    // Flush any pending records
    if (!block_records_.empty()) {
        const auto res = write_block_locked();
        if (res.is_failure()) {
            return res;
        }
    }

    // Write footer
    WSndlFooter ftr{};
    ftr.magic = kWSndlFooterMagic;
    ftr.version = 1;
    ftr.finalization_status = status;
    ftr.block_count = block_count_;
    ftr.total_records = total_records_;
    ftr.last_sequence = last_sequence_;
    ftr.total_instructions = total_instructions;

    std::uint8_t ftr_bytes[kWSndlFooterSize]{};
    encode_sndl_footer(ftr, ftr_bytes);

    file_.write(reinterpret_cast<const char*>(ftr_bytes), kWSndlFooterSize);
    if (!file_) {
        failed_ = true;
        return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Failed to write SNDL footer");
    }

    // If completed cleanly, seek to 0 and rewrite header with true counts
    if (status == SndlFinalizationStatus::Completed && !failed_) {
        file_.seekp(0, std::ios::beg);
        header_.total_record_count = total_records_;
        header_.total_instructions = total_instructions;

        std::uint8_t hdr_bytes[kWSndlHeaderSize]{};
        encode_sndl_header(header_, hdr_bytes);
        file_.write(reinterpret_cast<const char*>(hdr_bytes), kWSndlHeaderSize);
        if (!file_) {
            failed_ = true;
            return SndlResult::fail(SndlErrorCode::FileWriteFailed, "Failed to update SNDL header");
        }
    }

    file_.flush();
    file_.close();
    finalized_ = true;

    return SndlResult::ok();
}

void SndlWriter::close() noexcept {
    if (file_.is_open()) {
        file_.close();
    }
}

} // namespace winios::sndl
