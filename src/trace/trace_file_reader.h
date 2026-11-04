#pragma once

#include "trace_event.h"
#include "trace_serializer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace winios::trace {

class TraceFileReader {
public:
    TraceFileReader();
    ~TraceFileReader();

    // Non-copyable, movable
    TraceFileReader(const TraceFileReader&) = delete;
    TraceFileReader& operator=(const TraceFileReader&) = delete;
    TraceFileReader(TraceFileReader&& other) noexcept;
    TraceFileReader& operator=(TraceFileReader&& other) noexcept;

    // Opens a trace file and validates its header and finalization footer.
    bool open(const std::filesystem::path& path);
    TraceFileError open_with_error(const std::filesystem::path& path);

    // Reads the next event. Returns true on success, false on EOF or error.
    bool read_event(TraceEvent& out_event);

    // Reads up to max_count events. Returns count of events read.
    std::size_t read_events(TraceEvent* out_events, std::size_t max_count);

    // Reads all remaining events into a vector. Returns count of events read.
    std::size_t read_all_events(std::vector<TraceEvent>& out_events);

    // Closes the file.
    void close();

    // State & metadata queries
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_eof() const noexcept { return is_eof_; }
    [[nodiscard]] bool is_cleanly_finalized() const noexcept { return is_cleanly_finalized_; }
    [[nodiscard]] const TraceFileHeader& header() const noexcept { return header_; }
    [[nodiscard]] const TraceFileFooter& footer() const noexcept { return footer_; }
    [[nodiscard]] std::uint64_t total_events_read() const noexcept { return events_read_; }
    [[nodiscard]] std::uint64_t total_blocks_read() const noexcept { return blocks_read_; }
    [[nodiscard]] TraceFileError last_error() const noexcept { return last_error_; }
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept { return file_path_; }

private:
    bool load_next_block();

    std::filesystem::path file_path_{};
    void* file_handle_{nullptr}; // FILE* handle encapsulated

    TraceFileHeader header_{};
    TraceFileFooter footer_{};

    std::vector<TraceEvent> block_events_;
    std::size_t current_block_event_idx_{0};

    std::uint64_t events_read_{0};
    std::uint64_t blocks_read_{0};
    std::uint64_t file_size_{0};
    std::uint64_t current_offset_{0};

    bool is_open_{false};
    bool is_eof_{false};
    bool is_cleanly_finalized_{false};
    TraceFileError last_error_{TraceFileError::Success};
};

} // namespace winios::trace
