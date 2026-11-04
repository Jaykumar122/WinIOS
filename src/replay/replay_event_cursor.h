#pragma once

#include "sndl/sndl_format.h"
#include "sndl/sndl_types.h"
#include "replay/replay_profiler.h"
#include "replay/replay_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayEventCursor: Sequential Streaming Reader for SNDL Event Logs
// -----------------------------------------------------------------------------
class ReplayEventCursor {
public:
    ReplayEventCursor();
    ~ReplayEventCursor();

    ReplayEventCursor(const ReplayEventCursor&) = delete;
    ReplayEventCursor& operator=(const ReplayEventCursor&) = delete;
    ReplayEventCursor(ReplayEventCursor&&) noexcept;
    ReplayEventCursor& operator=(ReplayEventCursor&&) noexcept;

    // Opens an .sndl file and reads/validates header
    [[nodiscard]] ReplayResult open(const std::filesystem::path& sndl_path);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
    [[nodiscard]] bool has_next() const noexcept;
    [[nodiscard]] const sndl::SndlRecord& current() const;

    // Stage 21: Optional profile sink. Null disables all timing and count writes.
    void set_profiler(ReplayProfile* profile) noexcept { profiler_ = profile; }

    // Advances the cursor to the next record in the stream
    [[nodiscard]] ReplayResult advance();

    // Query helpers
    [[nodiscard]] const sndl::WSndlHeader& header() const noexcept { return header_; }
    [[nodiscard]] const sndl::WSndlFooter& footer() const noexcept { return footer_; }
    [[nodiscard]] bool is_footer_read() const noexcept { return footer_read_; }
    [[nodiscard]] std::uint64_t total_consumed() const noexcept { return total_consumed_; }
    [[nodiscard]] std::uint64_t total_expected() const noexcept { return header_.total_record_count; }
    [[nodiscard]] std::uint64_t last_consumed_sequence() const noexcept { return last_consumed_sequence_; }

    // Boundary query: checks if next record is an asynchronous input ready for injection
    [[nodiscard]] bool has_pending_asynchronous_input(std::uint16_t core_id, std::uint64_t ret_count) const noexcept;

private:
    [[nodiscard]] ReplayResult load_next_block();

    std::filesystem::path file_path_;
    std::ifstream file_;
    sndl::WSndlHeader header_{};
    sndl::WSndlFooter footer_{};
    std::vector<sndl::SndlRecord> current_block_records_;
    std::size_t current_record_index_{0};
    std::uint64_t total_consumed_{0};
    std::uint64_t last_consumed_sequence_{0};
    bool footer_read_{false};
    bool stream_ended_{false};
    ReplayProfile* profiler_{nullptr};
};

} // namespace winios::replay
