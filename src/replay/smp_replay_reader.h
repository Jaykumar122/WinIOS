#pragma once

#include "sndl/sndl_v2_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// SndlV2ReplayReader: Streaming, Bounded Sequential Reader for SNDL v2 Recordings
// -----------------------------------------------------------------------------
class SndlV2ReplayReader {
public:
    SndlV2ReplayReader();
    ~SndlV2ReplayReader();

    SndlV2ReplayReader(const SndlV2ReplayReader&) = delete;
    SndlV2ReplayReader& operator=(const SndlV2ReplayReader&) = delete;

    // Opens recording file, validates header, reads and validates footer
    [[nodiscard]] sndl::SndlV2Result open(const std::filesystem::path& path);

    // Closes file handle and clears buffer
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] const sndl::WSndlV2Header& header() const noexcept { return header_; }
    [[nodiscard]] const sndl::WSndlV2Footer& footer() const noexcept { return footer_; }
    [[nodiscard]] bool has_footer() const noexcept { return has_footer_; }
    [[nodiscard]] std::uint32_t core_count() const noexcept { return header_.core_count; }
    [[nodiscard]] std::uint64_t total_events() const noexcept { return header_.total_events; }

    // Check if more events remain in recording
    [[nodiscard]] bool has_next();

    // Inspect next record without consuming/advancing
    [[nodiscard]] sndl::SndlV2Result peek_next(sndl::SndlV2Record& out_record);

    // Consume next record from stream
    [[nodiscard]] sndl::SndlV2Result read_next(sndl::SndlV2Record& out_record);

    // Number of records currently read / consumed
    [[nodiscard]] std::uint64_t records_consumed() const noexcept { return records_consumed_; }

private:
    [[nodiscard]] sndl::SndlV2Result buffer_next_block();

    bool is_open_{false};
    std::filesystem::path path_;
    std::ifstream file_;
    sndl::WSndlV2Header header_{};
    sndl::WSndlV2Footer footer_{};
    bool has_footer_{false};

    std::uint64_t file_size_{0};
    std::uint64_t current_file_offset_{0};
    std::uint32_t current_block_index_{0};
    std::uint64_t records_consumed_{0};

    // Bounded block buffer
    std::vector<sndl::SndlV2Record> buffer_;
    std::size_t buffer_index_{0};
    std::uint64_t last_seen_sequence_{0};
};

} // namespace winios::replay
