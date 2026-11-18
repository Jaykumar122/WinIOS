#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace winios::devices {

#pragma pack(push, 8)
struct TimeSpecData {
    std::uint64_t monotonic_us{0};
    std::uint64_t wall_time_us{0};
    std::int32_t  timezone_offset_minutes{0};
    std::uint32_t timer_frequency_hz{1000000}; // 1 MHz resolution (microseconds)
};

struct TimerDescriptor {
    std::uint32_t timer_id{0};
    std::uint64_t deadline_us{0};
    std::uint64_t interval_us{0}; // 0 = one-shot, >0 = periodic
    bool          is_active{false};
};
#pragma pack(pop)

class VirtualClockService : public VirtualDeviceBase {
public:
    static constexpr std::uint32_t kDefaultClockDeviceId = 8;

    VirtualClockService();
    ~VirtualClockService() override = default;

    // Time queries
    [[nodiscard]] std::uint64_t now_monotonic_us() const;
    [[nodiscard]] std::uint64_t now_wall_time_us() const;
    [[nodiscard]] TimeSpecData  get_time_spec() const;

    // Timer management
    std::uint32_t create_timer(std::uint64_t delay_us, std::uint64_t interval_us = 0);
    bool cancel_timer(std::uint32_t timer_id);
    std::vector<std::uint32_t> poll_expired_timers();

    // Time offset manipulation (for simulation / testing)
    void advance_virtual_time(std::uint64_t delta_us);

    // Lifecycle
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    mutable std::mutex lock_;
    std::chrono::steady_clock::time_point boot_time_;
    std::uint64_t virtual_offset_us_{0};
    std::uint32_t next_timer_id_{1};
    std::unordered_map<std::uint32_t, TimerDescriptor> timers_;
};

} // namespace winios::devices
