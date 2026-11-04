#include "trace_file_writer.h"

#include <chrono>
#include <cstdio>

namespace winios::trace {

TraceFileWriter::TraceFileWriter(std::size_t block_capacity)
    : block_capacity_(block_capacity > 0 ? block_capacity : DEFAULT_WRITER_BLOCK_CAPACITY) {
    if (block_capacity_ > MAX_EVENTS_PER_BLOCK) {
        block_capacity_ = MAX_EVENTS_PER_BLOCK;
    }
    event_buffer_.reserve(block_capacity_);
    block_serialize_buffer_.resize(TRACE_BLOCK_HEADER_SIZE + block_capacity_ * TRACE_EVENT_DISK_SIZE);
}

TraceFileWriter::~TraceFileWriter() {
    if (is_open_) {
        close();
    }
}

TraceFileWriter::TraceFileWriter(TraceFileWriter&& other) noexcept
    : block_capacity_(other.block_capacity_),
      file_path_(std::move(other.file_path_)),
      file_handle_(other.file_handle_),
      event_buffer_(std::move(other.event_buffer_)),
      block_serialize_buffer_(std::move(other.block_serialize_buffer_)),
      total_events_(other.total_events_),
      total_blocks_(other.total_blocks_),
      is_open_(other.is_open_),
      last_error_(other.last_error_) {
    other.file_handle_ = nullptr;
    other.is_open_ = false;
}

TraceFileWriter& TraceFileWriter::operator=(TraceFileWriter&& other) noexcept {
    if (this != &other) {
        if (is_open_) {
            close();
        }
        block_capacity_         = other.block_capacity_;
        file_path_              = std::move(other.file_path_);
        file_handle_            = other.file_handle_;
        event_buffer_           = std::move(other.event_buffer_);
        block_serialize_buffer_ = std::move(other.block_serialize_buffer_);
        total_events_           = other.total_events_;
        total_blocks_           = other.total_blocks_;
        is_open_                = other.is_open_;
        last_error_             = other.last_error_;

        other.file_handle_ = nullptr;
        other.is_open_ = false;
    }
    return *this;
}

bool TraceFileWriter::is_open() const noexcept {
    return is_open_ && file_handle_ != nullptr;
}

bool TraceFileWriter::open(const std::filesystem::path& path, bool overwrite) {
    return open_with_error(path, overwrite) == TraceFileError::Success;
}

TraceFileError TraceFileWriter::open_with_error(const std::filesystem::path& path, bool overwrite) {
    if (is_open_) {
        last_error_ = TraceFileError::AlreadyOpen;
        return last_error_;
    }

    if (path.empty()) {
        last_error_ = TraceFileError::InvalidPath;
        return last_error_;
    }

    std::error_code ec;
    if (!overwrite && std::filesystem::exists(path, ec)) {
        last_error_ = TraceFileError::FileAlreadyExists;
        return last_error_;
    }

    FILE* fp = nullptr;
#if defined(_WIN32)
    _wfopen_s(&fp, path.c_str(), L"wb");
#else
    fp = std::fopen(path.string().c_str(), "wb");
#endif

    if (!fp) {
        last_error_ = TraceFileError::FileOpenFailed;
        return last_error_;
    }

    file_handle_ = fp;
    file_path_ = path;
    is_open_ = true;
    total_events_ = 0;
    total_blocks_ = 0;
    event_buffer_.clear();

    // Serialize and write File Header
    TraceFileHeader hdr{};
    hdr.creation_timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::uint8_t hdr_bytes[TRACE_FILE_HEADER_SIZE];
    TraceFileError err = serialize_file_header(hdr, hdr_bytes, sizeof(hdr_bytes));
    if (err != TraceFileError::Success) {
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = err;
        return last_error_;
    }

    if (std::fwrite(hdr_bytes, 1, TRACE_FILE_HEADER_SIZE, fp) != TRACE_FILE_HEADER_SIZE) {
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = TraceFileError::FileWriteFailed;
        return last_error_;
    }

    last_error_ = TraceFileError::Success;
    return TraceFileError::Success;
}

bool TraceFileWriter::write_event(const TraceEvent& event) {
    if (!is_open()) {
        last_error_ = TraceFileError::NotOpen;
        return false;
    }

    event_buffer_.push_back(event);
    if (event_buffer_.size() >= block_capacity_) {
        return flush_current_block();
    }

    return true;
}

bool TraceFileWriter::write_events(std::span<const TraceEvent> events) {
    return write_events(events.data(), events.size());
}

bool TraceFileWriter::write_events(const TraceEvent* events, std::size_t count) {
    if (!is_open()) {
        last_error_ = TraceFileError::NotOpen;
        return false;
    }
    if (!events || count == 0) return true;

    for (std::size_t i = 0; i < count; ++i) {
        if (!write_event(events[i])) {
            return false;
        }
    }
    return true;
}

bool TraceFileWriter::flush_current_block() {
    if (!is_open()) {
        last_error_ = TraceFileError::NotOpen;
        return false;
    }

    if (event_buffer_.empty()) {
        return true;
    }

    auto* fp = static_cast<FILE*>(file_handle_);
    const auto count = static_cast<std::uint32_t>(event_buffer_.size());
    const std::uint32_t payload_bytes = count * static_cast<std::uint32_t>(TRACE_EVENT_DISK_SIZE);

    // Serialize all events directly into payload offset of serialization buffer
    std::uint8_t* payload_ptr = block_serialize_buffer_.data() + TRACE_BLOCK_HEADER_SIZE;
    for (std::size_t i = 0; i < count; ++i) {
        TraceFileError err = serialize_trace_event(
            event_buffer_[i], payload_ptr + (i * TRACE_EVENT_DISK_SIZE), TRACE_EVENT_DISK_SIZE);
        if (err != TraceFileError::Success) {
            last_error_ = err;
            return false;
        }
    }

    // Build block header
    TraceBlockHeader blk{};
    blk.block_sequence = total_blocks_;
    blk.event_count    = count;
    blk.payload_bytes  = payload_bytes;
    blk.payload_crc32c = calculate_crc32c(payload_ptr, payload_bytes);

    TraceFileError err = serialize_block_header(blk, block_serialize_buffer_.data(), TRACE_BLOCK_HEADER_SIZE);
    if (err != TraceFileError::Success) {
        last_error_ = err;
        return false;
    }

    // Write block header + payload in one contiguous operation
    const std::size_t total_to_write = TRACE_BLOCK_HEADER_SIZE + payload_bytes;
    if (std::fwrite(block_serialize_buffer_.data(), 1, total_to_write, fp) != total_to_write) {
        last_error_ = TraceFileError::FileWriteFailed;
        return false;
    }

    total_events_ += count;
    total_blocks_ += 1;
    event_buffer_.clear();

    return true;
}

bool TraceFileWriter::flush() {
    if (!is_open()) {
        last_error_ = TraceFileError::NotOpen;
        return false;
    }

    if (!flush_current_block()) {
        return false;
    }

    auto* fp = static_cast<FILE*>(file_handle_);
    if (std::fflush(fp) != 0) {
        last_error_ = TraceFileError::FileFlushFailed;
        return false;
    }

    return true;
}

bool TraceFileWriter::close() {
    if (!is_open()) {
        return true;
    }

    // 1. Flush any pending buffered events as a block
    if (!flush_current_block()) {
        auto* fp = static_cast<FILE*>(file_handle_);
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        return false;
    }

    auto* fp = static_cast<FILE*>(file_handle_);

    // 2. Build and serialize Finalization Footer
    TraceFileFooter ftr{};
    ftr.total_events = total_events_;
    ftr.total_blocks = total_blocks_;
    ftr.finalization_status = TRACE_STATUS_CLEANLY_FINALIZED;

    std::uint8_t ftr_bytes[TRACE_FILE_FOOTER_SIZE];
    TraceFileError err = serialize_file_footer(ftr, ftr_bytes, sizeof(ftr_bytes));
    if (err != TraceFileError::Success) {
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = err;
        return false;
    }

    // 3. Write footer
    if (std::fwrite(ftr_bytes, 1, TRACE_FILE_FOOTER_SIZE, fp) != TRACE_FILE_FOOTER_SIZE) {
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = TraceFileError::FileWriteFailed;
        return false;
    }

    // 4. Final flush and close
    if (std::fflush(fp) != 0) {
        std::fclose(fp);
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = TraceFileError::FileFlushFailed;
        return false;
    }

    if (std::fclose(fp) != 0) {
        file_handle_ = nullptr;
        is_open_ = false;
        last_error_ = TraceFileError::FileCloseFailed;
        return false;
    }

    file_handle_ = nullptr;
    is_open_ = false;
    last_error_ = TraceFileError::Success;
    return true;
}

} // namespace winios::trace
