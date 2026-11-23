#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace winios::cpu {

struct PerCoreTimerState {
    std::uint64_t counter{0};
    std::uint64_t comparator{UINT64_MAX};
    std::uint32_t control{0}; // Bit 0: ENABLE, Bit 1: IMASK, Bit 2: ISTATUS
    bool interrupt_asserted{false};
    std::uint64_t total_timer_firings{0};
    std::uint64_t total_acks{0};
    std::uint64_t total_rearms{0};
    std::uint64_t missed_ticks{0};
    std::uint64_t last_rearm_elapsed_periods{0};
};

enum class TimerRearmStatus : std::uint32_t {
    Success = 0,
    InvalidPeriod = 1,
    InvalidCore = 2,
    CounterBehindDeadline = 3,
    MultiplicationOverflow = 4,
    DeadlineSaturated = 5
};

constexpr const char* timer_rearm_status_to_string(TimerRearmStatus status) noexcept {
    switch (status) {
        case TimerRearmStatus::Success:                return "Success";
        case TimerRearmStatus::InvalidPeriod:          return "InvalidPeriod";
        case TimerRearmStatus::InvalidCore:            return "InvalidCore";
        case TimerRearmStatus::CounterBehindDeadline:   return "CounterBehindDeadline";
        case TimerRearmStatus::MultiplicationOverflow: return "MultiplicationOverflow";
        case TimerRearmStatus::DeadlineSaturated:      return "DeadlineSaturated";
    }
    return "UnknownStatus";
}

struct TimerRearmResult {
    std::uint64_t elapsed_periods{0};
    std::uint64_t next_deadline{0};
    std::uint64_t missed_ticks{0};
    TimerRearmStatus status{TimerRearmStatus::Success};
};

class DeterministicVirtualTimer {
public:
    static constexpr std::size_t kMaxCores = 6; // iPhone 15 A16 Bionic 6 cores
    static constexpr std::uint64_t kDefaultFrequencyHz = 24000000ULL; // 24 MHz default AArch64
    static constexpr std::uint32_t kCtlEnable  = 0x01u;
    static constexpr std::uint32_t kCtlIMask   = 0x02u;
    static constexpr std::uint32_t kCtlIStatus = 0x04u;

    explicit DeterministicVirtualTimer(std::uint64_t freq_hz = kDefaultFrequencyHz);

    void reset() noexcept;

    void set_frequency(std::uint64_t freq_hz) noexcept { frequency_hz_ = freq_hz; }
    [[nodiscard]] std::uint64_t frequency() const noexcept { return frequency_hz_; }

    void set_control(std::size_t core_id, std::uint32_t ctl) noexcept;
    [[nodiscard]] std::uint32_t get_control(std::size_t core_id) const noexcept;

    void set_comparator(std::size_t core_id, std::uint64_t cval) noexcept;
    [[nodiscard]] std::uint64_t get_comparator(std::size_t core_id) const noexcept;

    void set_tval(std::size_t core_id, std::int32_t tval) noexcept;
    [[nodiscard]] std::int32_t get_tval(std::size_t core_id) const noexcept;

    void advance_counter(std::uint64_t ticks) noexcept;
    void advance_us(std::uint64_t microseconds) noexcept;

    [[nodiscard]] std::uint64_t get_counter(std::size_t core_id = 0) const noexcept;
    [[nodiscard]] bool is_interrupt_asserted(std::size_t core_id) const noexcept;

    void acknowledge(std::size_t core_id) noexcept;
    TimerRearmResult rearm_periodic(std::size_t core_id, std::uint64_t interval_ticks) noexcept;
    [[nodiscard]] std::uint64_t missed_ticks(std::size_t core_id) const noexcept;

    [[nodiscard]] const PerCoreTimerState& core_state(std::size_t core_id) const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    bool deserialize(const std::uint8_t* data, std::size_t size) noexcept;

private:
    std::uint64_t frequency_hz_{kDefaultFrequencyHz};
    std::array<PerCoreTimerState, kMaxCores> cores_{};

    void evaluate_core_timer(std::size_t core_id) noexcept;
};

} // namespace winios::cpu
