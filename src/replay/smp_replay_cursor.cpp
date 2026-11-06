#include "replay/smp_replay_cursor.h"

namespace winios::replay {

SmpReplayEventCursor::SmpReplayEventCursor() = default;

SmpReplayEventCursor::~SmpReplayEventCursor() {
    close();
}

sndl::SndlV2Result SmpReplayEventCursor::open(const std::filesystem::path& path, bool strict_monotonicity) {
    close();
    strict_monotonicity_ = strict_monotonicity;

    const auto res = reader_.open(path);
    if (res.is_failure()) {
        return res;
    }

    current_sequence_ = reader_.header().initial_global_sequence;
    consumed_count_ = 0;
    last_record_ = std::nullopt;
    return sndl::SndlV2Result::ok();
}

void SmpReplayEventCursor::close() noexcept {
    reader_.close();
    current_sequence_ = 0;
    consumed_count_ = 0;
    last_record_ = std::nullopt;
}

sndl::SndlV2Result SmpReplayEventCursor::peek(sndl::SndlV2Record& out_record) {
    return reader_.peek_next(out_record);
}

bool SmpReplayEventCursor::peek_type_is(sndl::SndlV2EventType expected_type) {
    sndl::SndlV2Record rec{};
    if (reader_.peek_next(rec).is_failure()) {
        return false;
    }
    return rec.event_type == expected_type;
}

sndl::SndlV2Result SmpReplayEventCursor::validate_sequence_continuity(const sndl::SndlV2Record& rec) {
    if (consumed_count_ == 0) {
        if (rec.global_event_sequence < reader_.header().initial_global_sequence) {
            return sndl::SndlV2Result::fail(
                sndl::SndlV2ErrorCode::InvalidSequence,
                "Initial event sequence (" + std::to_string(rec.global_event_sequence) +
                ") < header initial_global_sequence (" + std::to_string(reader_.header().initial_global_sequence) + ")"
            );
        }
    } else {
        if (strict_monotonicity_) {
            if (rec.global_event_sequence != current_sequence_ + 1) {
                return sndl::SndlV2Result::fail(
                    sndl::SndlV2ErrorCode::InvalidSequence,
                    "Sequence gap detected: expected " + std::to_string(current_sequence_ + 1) +
                    ", got " + std::to_string(rec.global_event_sequence)
                );
            }
        } else {
            if (rec.global_event_sequence <= current_sequence_) {
                return sndl::SndlV2Result::fail(
                    sndl::SndlV2ErrorCode::InvalidSequence,
                    "Sequence monotonicity violation: previous=" + std::to_string(current_sequence_) +
                    ", current=" + std::to_string(rec.global_event_sequence)
                );
            }
        }
    }
    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SmpReplayEventCursor::consume_expected_type(
    sndl::SndlV2EventType expected_type,
    sndl::SndlV2Record& out_record) {

    sndl::SndlV2Record next_rec{};
    const auto peek_res = reader_.peek_next(next_rec);
    if (peek_res.is_failure()) {
        return peek_res;
    }

    if (next_rec.event_type != expected_type) {
        return sndl::SndlV2Result::fail(
            sndl::SndlV2ErrorCode::InvalidPayload,
            "Event type mismatch: expected " + std::string(sndl::sndl_v2_event_type_name(expected_type)) +
            ", got " + std::string(sndl::sndl_v2_event_type_name(next_rec.event_type))
        );
    }

    const auto seq_res = validate_sequence_continuity(next_rec);
    if (seq_res.is_failure()) {
        return seq_res;
    }

    // Atomic advance
    (void)reader_.read_next(out_record);
    current_sequence_ = out_record.global_event_sequence;
    consumed_count_++;
    last_record_ = out_record;

    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SmpReplayEventCursor::consume(
    const sndl::SndlV2Record& expected_record,
    sndl::SndlV2Record& out_record) {

    sndl::SndlV2Record next_rec{};
    const auto peek_res = reader_.peek_next(next_rec);
    if (peek_res.is_failure()) {
        return peek_res;
    }

    if (next_rec.event_type != expected_record.event_type) {
        return sndl::SndlV2Result::fail(
            sndl::SndlV2ErrorCode::InvalidPayload,
            "Event type mismatch: expected " + std::string(sndl::sndl_v2_event_type_name(expected_record.event_type)) +
            ", got " + std::string(sndl::sndl_v2_event_type_name(next_rec.event_type))
        );
    }

    if (next_rec.core_id != expected_record.core_id) {
        return sndl::SndlV2Result::fail(
            sndl::SndlV2ErrorCode::InvalidCoreId,
            "Core ID mismatch: expected " + std::to_string(expected_record.core_id) +
            ", got " + std::to_string(next_rec.core_id)
        );
    }

    const auto seq_res = validate_sequence_continuity(next_rec);
    if (seq_res.is_failure()) {
        return seq_res;
    }

    (void)reader_.read_next(out_record);
    current_sequence_ = out_record.global_event_sequence;
    consumed_count_++;
    last_record_ = out_record;

    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SmpReplayEventCursor::consume_next(sndl::SndlV2Record& out_record) {
    sndl::SndlV2Record next_rec{};
    const auto peek_res = reader_.peek_next(next_rec);
    if (peek_res.is_failure()) {
        return peek_res;
    }

    const auto seq_res = validate_sequence_continuity(next_rec);
    if (seq_res.is_failure()) {
        return seq_res;
    }

    (void)reader_.read_next(out_record);
    current_sequence_ = out_record.global_event_sequence;
    consumed_count_++;
    last_record_ = out_record;

    return sndl::SndlV2Result::ok();
}

sndl::SndlV2Result SmpReplayEventCursor::seek_to_sequence(const std::filesystem::path& path, std::uint64_t target_sequence) {
    const auto open_res = open(path, strict_monotonicity_);
    if (open_res.is_failure()) {
        return open_res;
    }

    if (target_sequence == 0 || target_sequence == current_sequence_) {
        return sndl::SndlV2Result::ok();
    }

    sndl::SndlV2Record rec{};
    while (has_next() && current_sequence_ < target_sequence) {
        const auto res = consume_next(rec);
        if (res.is_failure()) {
            return res;
        }
    }

    if (current_sequence_ != target_sequence) {
        return sndl::SndlV2Result::fail(
            sndl::SndlV2ErrorCode::InvalidSequence,
            "Target sequence " + std::to_string(target_sequence) + " not reached (stopped at " + std::to_string(current_sequence_) + ")"
        );
    }

    return sndl::SndlV2Result::ok();
}

} // namespace winios::replay
