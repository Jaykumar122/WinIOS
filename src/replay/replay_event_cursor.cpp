#include "replay/replay_event_cursor.h"
#include "sndl/sndl_event_encoder.h"
#include "trace/trace_serializer.h"

#include <cstring>

namespace winios::replay {

ReplayEventCursor::ReplayEventCursor() = default;

ReplayEventCursor::~ReplayEventCursor() {
    close();
}

ReplayEventCursor::ReplayEventCursor(ReplayEventCursor&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      file_(std::move(other.file_)),
      header_(other.header_),
      footer_(other.footer_),
      current_block_records_(std::move(other.current_block_records_)),
      current_record_index_(other.current_record_index_),
      total_consumed_(other.total_consumed_),
      last_consumed_sequence_(other.last_consumed_sequence_),
      footer_read_(other.footer_read_),
      stream_ended_(other.stream_ended_) {
    other.close();
}

ReplayEventCursor& ReplayEventCursor::operator=(ReplayEventCursor&& other) noexcept {
    if (this != &other) {
        close();
        file_path_ = std::move(other.file_path_);
        file_ = std::move(other.file_);
        header_ = other.header_;
        footer_ = other.footer_;
        current_block_records_ = std::move(other.current_block_records_);
        current_record_index_ = other.current_record_index_;
        total_consumed_ = other.total_consumed_;
        last_consumed_sequence_ = other.last_consumed_sequence_;
        footer_read_ = other.footer_read_;
        stream_ended_ = other.stream_ended_;
        other.close();
    }
    return *this;
}

ReplayResult ReplayEventCursor::open(const std::filesystem::path& sndl_path) {
    close();
    file_path_ = sndl_path;
    file_.open(file_path_, std::ios::binary | std::ios::in);
    if (!file_.is_open()) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError,
                                  "Failed to open SNDL file: " + file_path_.string());
    }

    // Read and decode WSndlHeader (64 bytes)
    std::uint8_t hdr_bytes[sndl::kWSndlHeaderSize]{};
    file_.read(reinterpret_cast<char*>(hdr_bytes), sizeof(hdr_bytes));
    if (static_cast<std::size_t>(file_.gcount()) < sizeof(hdr_bytes)) {
        close();
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed,
                                  "SNDL file too short for header");
    }

    const auto dec_res = sndl::decode_sndl_header(hdr_bytes, sizeof(hdr_bytes), header_);
    if (dec_res.is_failure()) {
        close();
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, dec_res.message);
    }

    // Load initial block
    return load_next_block();
}

void ReplayEventCursor::close() noexcept {
    if (file_.is_open()) {
        file_.close();
    }
    header_ = {};
    footer_ = {};
    current_block_records_.clear();
    current_record_index_ = 0;
    total_consumed_ = 0;
    last_consumed_sequence_ = 0;
    footer_read_ = false;
    stream_ended_ = false;
}

bool ReplayEventCursor::has_next() const noexcept {
    if (!file_.is_open()) return false;
    return current_record_index_ < current_block_records_.size() || !stream_ended_;
}

const sndl::SndlRecord& ReplayEventCursor::current() const {
    static const sndl::SndlRecord kEmptyRecord{};
    if (current_record_index_ < current_block_records_.size()) {
        return current_block_records_[current_record_index_];
    }
    return kEmptyRecord;
}

ReplayResult ReplayEventCursor::advance() {
    if (!has_next()) {
        return ReplayResult::fail(ReplayErrorCode::EventCursorError, "Attempted to advance past end of SNDL stream");
    }

    if (current_record_index_ < current_block_records_.size()) {
        last_consumed_sequence_ = current_block_records_[current_record_index_].sequence_num;
        ++current_record_index_;
        ++total_consumed_;
        if (profiler_ != nullptr) {
            ++profiler_->cursor_advances;
        }
    }

    if (current_record_index_ >= current_block_records_.size() && !stream_ended_) {
        return load_next_block();
    }

    return ReplayResult::ok();
}

bool ReplayEventCursor::has_pending_asynchronous_input(std::uint16_t core_id, std::uint64_t ret_count) const noexcept {
    if (!has_next()) return false;
    const auto& rec = current();
    return rec.core_id == core_id &&
           rec.instruction_retirement_count <= ret_count &&
           (rec.boundary_type == sndl::SndlBoundaryType::AsynchronousInput ||
            rec.boundary_type == sndl::SndlBoundaryType::ExceptionBoundary ||
            rec.boundary_type == sndl::SndlBoundaryType::BetweenInstructions ||
            rec.boundary_type == sndl::SndlBoundaryType::BeforeInstruction);
}

ReplayResult ReplayEventCursor::load_next_block() {
    ReplayProfileScope block_scope(profiler_ ? &profiler_->cursor_block_load : nullptr);
    current_block_records_.clear();
    current_record_index_ = 0;

    if (stream_ended_ || !file_.is_open() || file_.peek() == EOF) {
        stream_ended_ = true;
        return ReplayResult::ok();
    }

    // Inspect next magic: could be SBLK (4 bytes) or WSNDLFTR (8 bytes)
    std::uint8_t probe[8]{};
    const auto pos = file_.tellg();
    file_.read(reinterpret_cast<char*>(probe), sizeof(probe));
    const auto read_cnt = file_.gcount();
    file_.seekg(pos);

    if (read_cnt < 4) {
        stream_ended_ = true;
        return ReplayResult::ok();
    }

    // Check for WSNDLFTR
    if (std::memcmp(probe, sndl::kWSndlFooterMagic.data(), 8) == 0) {
        std::uint8_t ftr_bytes[sndl::kWSndlFooterSize]{};
        file_.read(reinterpret_cast<char*>(ftr_bytes), sizeof(ftr_bytes));
        if (static_cast<std::size_t>(file_.gcount()) < sizeof(ftr_bytes)) {
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, "Truncated SNDL footer");
        }
        const auto ftr_res = sndl::decode_sndl_footer(ftr_bytes, sizeof(ftr_bytes), footer_);
        if (ftr_res.is_failure()) {
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, ftr_res.message);
        }
        footer_read_ = true;
        stream_ended_ = true;
        return ReplayResult::ok();
    }

    // Check for SBLK
    if (std::memcmp(probe, sndl::kWSndlBlockMagic.data(), 4) != 0) {
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, "Unrecognized block magic in SNDL file");
    }

    // Read WSndlBlockHeader (48 bytes)
    std::uint8_t blk_bytes[sndl::kWSndlBlockHeaderSize]{};
    file_.read(reinterpret_cast<char*>(blk_bytes), sizeof(blk_bytes));
    if (static_cast<std::size_t>(file_.gcount()) < sizeof(blk_bytes)) {
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, "Truncated SNDL block header");
    }

    sndl::WSndlBlockHeader blk{};
    const auto blk_res = sndl::decode_sndl_block_header(blk_bytes, sizeof(blk_bytes), blk);
    if (blk_res.is_failure()) {
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, blk_res.message);
    }

    // Read payload records
    std::vector<std::uint8_t> payload(blk.payload_bytes);
    file_.read(reinterpret_cast<char*>(payload.data()), blk.payload_bytes);
    if (static_cast<std::size_t>(file_.gcount()) < blk.payload_bytes) {
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, "Truncated block record payload");
    }

    std::uint32_t calc_crc = 0;
    {
        ReplayProfileScope crc_scope(profiler_ ? &profiler_->cursor_block_crc : nullptr);
        calc_crc = trace::calculate_crc32c(payload.data(), blk.payload_bytes);
    }
    if (profiler_ != nullptr) {
        ++profiler_->blocks_loaded;
        profiler_->block_bytes_read += blk.payload_bytes;
        ++profiler_->crc_calls;
        profiler_->crc_bytes += blk.payload_bytes;
    }
    if (calc_crc != blk.payload_crc32c) {
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, "Block payload CRC32C mismatch");
    }

    {
        ReplayProfileScope decode_scope(profiler_ ? &profiler_->cursor_block_decode : nullptr);
        current_block_records_.resize(blk.record_count);
        for (std::uint32_t i = 0; i < blk.record_count; ++i) {
            const auto dec_rec = sndl::decode_record(
                payload.data() + i * sndl::kWSndlRecordDiskSize,
                sndl::kWSndlRecordDiskSize,
                current_block_records_[i]
            );
            if (dec_rec.is_failure()) {
                return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, dec_rec.message);
            }
        }
    }
    if (profiler_ != nullptr) {
        profiler_->records_decoded += blk.record_count;
    }

    return ReplayResult::ok();
}

} // namespace winios::replay
