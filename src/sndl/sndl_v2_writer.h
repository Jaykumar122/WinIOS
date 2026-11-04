#pragma once

#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace winios::sndl {

struct SndlV2WriterConfig {
    std::uint32_t records_per_block{kDefaultV2RecordsPerBlock};
    std::size_t max_buffer_records{kDefaultV2RecorderQueueCapacity};
};

class SndlV2Writer {
public:
    SndlV2Writer();
    explicit SndlV2Writer(SndlV2WriterConfig config);
    ~SndlV2Writer();

    SndlV2Writer(const SndlV2Writer&) = delete;
    SndlV2Writer& operator=(const SndlV2Writer&) = delete;
    SndlV2Writer(SndlV2Writer&& other) noexcept;
    SndlV2Writer& operator=(SndlV2Writer&& other) noexcept;

    // Open file and write initial placeholder header
    [[nodiscard]] SndlV2Result open(const std::filesystem::path& path, const WSndlV2Header& header);

    // Append record to writer buffer (flushes blocks automatically)
    [[nodiscard]] SndlV2Result write_record(const SndlV2Record& record);

    // Finalize recording, write pending block and footer, and update header
    [[nodiscard]] SndlV2Result finalize(SndlV2CompletionStatus status = SndlV2CompletionStatus::Completed);

    // Close without full finalization
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
    [[nodiscard]] std::uint64_t total_events_written() const noexcept { return total_events_written_; }
    [[nodiscard]] std::uint32_t total_blocks_written() const noexcept { return block_index_; }
    [[nodiscard]] std::uint64_t dropped_events() const noexcept { return dropped_events_; }
    [[nodiscard]] bool has_overflowed() const noexcept { return overflow_; }
    void mark_overflow() noexcept { overflow_ = true; }

private:
    [[nodiscard]] SndlV2Result flush_current_block();

    SndlV2WriterConfig config_;
    std::filesystem::path file_path_;
    std::ofstream file_;
    WSndlV2Header header_{};
    std::vector<std::uint8_t> block_payload_buffer_;
    std::uint32_t block_records_count_{0};
    std::uint64_t block_first_seq_{0};
    std::uint64_t block_last_seq_{0};

    std::uint32_t block_index_{0};
    std::uint64_t total_events_written_{0};
    std::uint64_t first_global_seq_{0};
    std::uint64_t last_global_seq_{0};
    std::uint64_t last_global_retirement_seq_{0};
    std::uint64_t dropped_events_{0};
    bool overflow_{false};
    bool finalized_{false};
};

} // namespace winios::sndl
