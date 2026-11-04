#pragma once

#include "trace_event.h"
#include "trace_serializer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace winios::trace {

constexpr std::size_t DEFAULT_WRITER_BLOCK_CAPACITY = 512; // 512 events = 32 KiB payload

class TraceFileWriter {
public:
    explicit TraceFileWriter(std::size_t block_capacity = DEFAULT_WRITER_BLOCK_CAPACITY);
    ~TraceFileWriter();

    // Non-copyable, movable
    TraceFileWriter(const TraceFileWriter&) = delete;
    TraceFileWriter& operator=(const TraceFileWriter&) = delete;
    TraceFileWriter(TraceFileWriter&& other) noexcept;
    TraceFileWriter& operator=(TraceFileWriter&& other) noexcept;

    // Opens a trace file for binary writing.
    // Fails with FileAlreadyExists if path exists and overwrite is false.
    bool open(const std::filesystem::path& path, bool overwrite = false);
    TraceFileError open_with_error(const std::filesystem::path& path, bool overwrite = false);

    // Appends a single event to the current block buffer. Writes out full blocks.
    bool write_event(const TraceEvent& event);
    bool write_events(std::span<const TraceEvent> events);
    bool write_events(const TraceEvent* events, std::size_t count);

    // Flushes any partially filled event block to disk and flushes underlying file.
    bool flush();

    // Finalizes the trace file by writing any remaining events and appending the final footer.
    bool close();

    // State & statistics queries
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::uint64_t events_written() const noexcept { return total_events_; }
    [[nodiscard]] std::uint64_t blocks_written() const noexcept { return total_blocks_; }
    [[nodiscard]] TraceFileError last_error() const noexcept { return last_error_; }
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept { return file_path_; }

private:
    bool flush_current_block();

    std::size_t block_capacity_;
    std::filesystem::path file_path_{};
    void* file_handle_{nullptr}; // FILE* handle encapsulated to avoid C stdio leaks in header

    std::vector<TraceEvent> event_buffer_;
    std::vector<std::uint8_t> block_serialize_buffer_;

    std::uint64_t total_events_{0};
    std::uint64_t total_blocks_{0};
    bool is_open_{false};
    TraceFileError last_error_{TraceFileError::Success};
};

} // namespace winios::trace
