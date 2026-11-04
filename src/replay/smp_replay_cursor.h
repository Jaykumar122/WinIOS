#pragma once

#include "replay/smp_replay_reader.h"
#include "replay/smp_replay_types.h"
#include "sndl/sndl_v2_types.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// SmpReplayEventCursor: Atomic Sequential Consumption and Verification of Events
// -----------------------------------------------------------------------------
class SmpReplayEventCursor {
public:
    SmpReplayEventCursor();
    ~SmpReplayEventCursor();

    // Opens underlying SNDL v2 reader and initializes cursor position
    [[nodiscard]] sndl::SndlV2Result open(const std::filesystem::path& path, bool strict_monotonicity = true);

    // Closes cursor and reader
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return reader_.is_open(); }
    [[nodiscard]] bool has_next() { return reader_.has_next(); }
    [[nodiscard]] const sndl::WSndlV2Header& header() const noexcept { return reader_.header(); }
    [[nodiscard]] const sndl::WSndlV2Footer& footer() const noexcept { return reader_.footer(); }
    [[nodiscard]] bool has_footer() const noexcept { return reader_.has_footer(); }

    [[nodiscard]] std::uint64_t current_sequence() const noexcept { return current_sequence_; }
    [[nodiscard]] std::uint64_t consumed_count() const noexcept { return consumed_count_; }
    [[nodiscard]] const std::optional<sndl::SndlV2Record>& last_record() const noexcept { return last_record_; }

    // Inspect next event without advancing cursor
    [[nodiscard]] sndl::SndlV2Result peek(sndl::SndlV2Record& out_record);

    // Checks if next record exists and has specific event type
    [[nodiscard]] bool peek_type_is(sndl::SndlV2EventType expected_type);

    // Validates and consumes next event matching expected type
    [[nodiscard]] sndl::SndlV2Result consume_expected_type(
        sndl::SndlV2EventType expected_type,
        sndl::SndlV2Record& out_record
    );

    // Validates and consumes next event matching expected record
    [[nodiscard]] sndl::SndlV2Result consume(
        const sndl::SndlV2Record& expected_record,
        sndl::SndlV2Record& out_record
    );

    // Raw consumption of next event without semantic type checks (validates sequence continuity)
    [[nodiscard]] sndl::SndlV2Result consume_next(sndl::SndlV2Record& out_record);

private:
    [[nodiscard]] sndl::SndlV2Result validate_sequence_continuity(const sndl::SndlV2Record& rec);

    SndlV2ReplayReader reader_;
    bool strict_monotonicity_{true};
    std::uint64_t current_sequence_{0};
    std::uint64_t consumed_count_{0};
    std::optional<sndl::SndlV2Record> last_record_{std::nullopt};
};

} // namespace winios::replay
