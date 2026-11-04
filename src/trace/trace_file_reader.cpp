#include "trace_file_reader.h"

#include <cstdio>
#include <vector>

namespace winios::trace {

TraceFileReader::TraceFileReader() = default;

TraceFileReader::~TraceFileReader() {
    close();
}

TraceFileReader::TraceFileReader(TraceFileReader&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      file_handle_(other.file_handle_),
      header_(other.header_),
      footer_(other.footer_),
      block_events_(std::move(other.block_events_)),
      current_block_event_idx_(other.current_block_event_idx_),
      events_read_(other.events_read_),
      blocks_read_(other.blocks_read_),
      file_size_(other.file_size_),
      current_offset_(other.current_offset_),
      is_open_(other.is_open_),
      is_eof_(other.is_eof_),
      is_cleanly_finalized_(other.is_cleanly_finalized_),
      last_error_(other.last_error_) {
    other.file_handle_ = nullptr;
    other.is_open_ = false;
}

TraceFileReader& TraceFileReader::operator=(TraceFileReader&& other) noexcept {
    if (this != &other) {
        close();
        file_path_               = std::move(other.file_path_);
        file_handle_             = other.file_handle_;
        header_                  = other.header_;
        footer_                  = other.footer_;
        block_events_            = std::move(other.block_events_);
        current_block_event_idx_ = other.current_block_event_idx_;
        events_read_             = other.events_read_;
        blocks_read_             = other.blocks_read_;
        file_size_               = other.file_size_;
        current_offset_          = other.current_offset_;
        is_open_                 = other.is_open_;
        is_eof_                  = other.is_eof_;
        is_cleanly_finalized_    = other.is_cleanly_finalized_;
        last_error_              = other.last_error_;

        other.file_handle_ = nullptr;
        other.is_open_ = false;
    }
    return *this;
}

bool TraceFileReader::is_open() const noexcept {
    return is_open_ && file_handle_ != nullptr;
}

bool TraceFileReader::open(const std::filesystem::path& path) {
    return open_with_error(path) == TraceFileError::Success;
}

TraceFileError TraceFileReader::open_with_error(const std::filesystem::path& path) {
    if (is_open_) {
        close();
    }

    if (path.empty()) {
        last_error_ = TraceFileError::InvalidPath;
        return last_error_;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        last_error_ = TraceFileError::FileOpenFailed;
        return last_error_;
    }

    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        last_error_ = TraceFileError::FileOpenFailed;
        return last_error_;
    }

    if (sz > MAX_TRACE_FILE_SIZE) {
        last_error_ = TraceFileError::ArithmeticOverflow;
        return last_error_;
    }

    if (sz < TRACE_FILE_HEADER_SIZE + TRACE_FILE_FOOTER_SIZE) {
        last_error_ = TraceFileError::TruncatedFile;
        return last_error_;
    }

    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"rb");
#else
    fp = std::fopen(path.string().c_str(), "rb");
#endif

    if (!fp) {
        last_error_ = TraceFileError::FileOpenFailed;
        return last_error_;
    }

    file_handle_ = fp;
    file_path_ = path;
    file_size_ = sz;
    events_read_ = 0;
    blocks_read_ = 0;
    current_offset_ = 0;
    block_events_.clear();
    current_block_event_idx_ = 0;
    is_open_ = true;
    is_eof_ = false;
    is_cleanly_finalized_ = false;

    // 1. Read and validate File Header (first 64 bytes)
    std::uint8_t hdr_bytes[TRACE_FILE_HEADER_SIZE];
    if (std::fread(hdr_bytes, 1, TRACE_FILE_HEADER_SIZE, fp) != TRACE_FILE_HEADER_SIZE) {
        close();
        last_error_ = TraceFileError::TruncatedFile;
        return last_error_;
    }

    TraceFileError err = deserialize_file_header(hdr_bytes, sizeof(hdr_bytes), header_);
    if (err != TraceFileError::Success) {
        close();
        last_error_ = err;
        return last_error_;
    }

    // 2. Seek to Footer position and validate (last 64 bytes)
#if defined(_WIN32)
    if (_fseeki64(fp, static_cast<__int64>(file_size_ - TRACE_FILE_FOOTER_SIZE), SEEK_SET) != 0) {
#else
    if (std::fseek(fp, static_cast<long>(file_size_ - TRACE_FILE_FOOTER_SIZE), SEEK_SET) != 0) {
#endif
        close();
        last_error_ = TraceFileError::FileReadFailed;
        return last_error_;
    }

    std::uint8_t ftr_bytes[TRACE_FILE_FOOTER_SIZE];
    if (std::fread(ftr_bytes, 1, TRACE_FILE_FOOTER_SIZE, fp) != TRACE_FILE_FOOTER_SIZE) {
        close();
        last_error_ = TraceFileError::TruncatedFile;
        return last_error_;
    }

    err = deserialize_file_footer(ftr_bytes, sizeof(ftr_bytes), footer_);
    if (err != TraceFileError::Success) {
        close();
        last_error_ = err;
        return last_error_;
    }

    is_cleanly_finalized_ = true;

    // 3. Seek back to start of first block (offset 64)
#if defined(_WIN32)
    _fseeki64(fp, TRACE_FILE_HEADER_SIZE, SEEK_SET);
#else
    std::fseek(fp, TRACE_FILE_HEADER_SIZE, SEEK_SET);
#endif
    current_offset_ = TRACE_FILE_HEADER_SIZE;

    last_error_ = TraceFileError::Success;
    return TraceFileError::Success;
}

bool TraceFileReader::load_next_block() {
    if (!is_open() || is_eof_) {
        return false;
    }

    const std::uint64_t footer_start_offset = file_size_ - TRACE_FILE_FOOTER_SIZE;
    if (current_offset_ >= footer_start_offset) {
        is_eof_ = true;
        // Verify stream accounting invariants
        if (events_read_ != footer_.total_events || blocks_read_ != footer_.total_blocks) {
            last_error_ = TraceFileError::InvalidEventCount;
            return false;
        }
        last_error_ = TraceFileError::Success;
        return false;
    }

    auto* fp = static_cast<FILE*>(file_handle_);

    // Read Block Header (32 bytes)
    if (current_offset_ + TRACE_BLOCK_HEADER_SIZE > footer_start_offset) {
        last_error_ = TraceFileError::TruncatedFile;
        return false;
    }

    std::uint8_t blk_hdr_bytes[TRACE_BLOCK_HEADER_SIZE];
    if (std::fread(blk_hdr_bytes, 1, TRACE_BLOCK_HEADER_SIZE, fp) != TRACE_BLOCK_HEADER_SIZE) {
        last_error_ = TraceFileError::FileReadFailed;
        return false;
    }

    TraceBlockHeader blk{};
    TraceFileError err = deserialize_block_header(blk_hdr_bytes, sizeof(blk_hdr_bytes), blk);
    if (err != TraceFileError::Success) {
        last_error_ = err;
        return false;
    }

    if (blk.block_sequence != blocks_read_) {
        last_error_ = TraceFileError::InvalidBlockLength;
        return false;
    }

    // Check payload boundaries
    if (current_offset_ + TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes > footer_start_offset) {
        last_error_ = TraceFileError::TruncatedFile;
        return false;
    }

    // Read payload
    std::vector<std::uint8_t> payload(blk.payload_bytes);
    if (std::fread(payload.data(), 1, blk.payload_bytes, fp) != blk.payload_bytes) {
        last_error_ = TraceFileError::FileReadFailed;
        return false;
    }

    // Check payload CRC32C
    const std::uint32_t computed_crc = calculate_crc32c(payload.data(), blk.payload_bytes);
    if (computed_crc != blk.payload_crc32c) {
        last_error_ = TraceFileError::ChecksumMismatch;
        return false;
    }

    // Deserialize all events in block
    block_events_.resize(blk.event_count);
    for (std::uint32_t i = 0; i < blk.event_count; ++i) {
        err = deserialize_trace_event(
            payload.data() + (i * TRACE_EVENT_DISK_SIZE), TRACE_EVENT_DISK_SIZE, block_events_[i]);
        if (err != TraceFileError::Success) {
            last_error_ = err;
            return false;
        }
    }

    current_block_event_idx_ = 0;
    current_offset_ += TRACE_BLOCK_HEADER_SIZE + blk.payload_bytes;
    blocks_read_ += 1;

    last_error_ = TraceFileError::Success;
    return true;
}

bool TraceFileReader::read_event(TraceEvent& out_event) {
    if (!is_open()) {
        last_error_ = TraceFileError::NotOpen;
        return false;
    }

    if (current_block_event_idx_ < block_events_.size()) {
        out_event = block_events_[current_block_event_idx_++];
        events_read_ += 1;
        return true;
    }

    if (!load_next_block()) {
        return false;
    }

    if (current_block_event_idx_ < block_events_.size()) {
        out_event = block_events_[current_block_event_idx_++];
        events_read_ += 1;
        return true;
    }

    return false;
}

std::size_t TraceFileReader::read_events(TraceEvent* out_events, std::size_t max_count) {
    if (!out_events || max_count == 0) return 0;

    std::size_t count = 0;
    while (count < max_count) {
        if (!read_event(out_events[count])) {
            break;
        }
        ++count;
    }
    return count;
}

std::size_t TraceFileReader::read_all_events(std::vector<TraceEvent>& out_events) {
    TraceEvent ev;
    std::size_t count = 0;
    while (read_event(ev)) {
        out_events.push_back(ev);
        ++count;
    }
    return count;
}

void TraceFileReader::close() {
    if (file_handle_) {
        std::fclose(static_cast<FILE*>(file_handle_));
        file_handle_ = nullptr;
    }
    is_open_ = false;
    is_eof_ = true;
    block_events_.clear();
    current_block_event_idx_ = 0;
}

} // namespace winios::trace
