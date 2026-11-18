#include "devices/clock_service.h"
#include "security/capability_manager.h"

namespace winios::devices {

VirtualClockService::VirtualClockService()
    : VirtualDeviceBase(kDefaultClockDeviceId,
                        1,
                        DeviceClass::Clock,
                        "WinIOS System Clock & Timer",
                        "WinIOS Timing Subsystem",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::DeviceEnumerate)),
      boot_time_(std::chrono::steady_clock::now()) {}

std::uint64_t VirtualClockService::now_monotonic_us() const {
    std::lock_guard<std::mutex> guard(lock_);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - boot_time_).count();
    return static_cast<std::uint64_t>(elapsed) + virtual_offset_us_;
}

std::uint64_t VirtualClockService::now_wall_time_us() const {
    const auto epoch_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> guard(lock_);
    return static_cast<std::uint64_t>(epoch_us) + virtual_offset_us_;
}

TimeSpecData VirtualClockService::get_time_spec() const {
    TimeSpecData spec{};
    spec.monotonic_us = now_monotonic_us();
    spec.wall_time_us = now_wall_time_us();
    spec.timezone_offset_minutes = 0; // UTC
    spec.timer_frequency_hz = 1000000;
    return spec;
}

std::uint32_t VirtualClockService::create_timer(std::uint64_t delay_us, std::uint64_t interval_us) {
    std::lock_guard<std::mutex> guard(lock_);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - boot_time_).count();
    const std::uint64_t current_mono = static_cast<std::uint64_t>(elapsed) + virtual_offset_us_;

    const std::uint32_t tid = next_timer_id_++;
    TimerDescriptor desc{};
    desc.timer_id = tid;
    desc.deadline_us = current_mono + delay_us;
    desc.interval_us = interval_us;
    desc.is_active = true;

    timers_[tid] = desc;
    return tid;
}

bool VirtualClockService::cancel_timer(std::uint32_t timer_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = timers_.find(timer_id);
    if (it != timers_.end() && it->second.is_active) {
        it->second.is_active = false;
        timers_.erase(it);
        return true;
    }
    return false;
}

std::vector<std::uint32_t> VirtualClockService::poll_expired_timers() {
    std::lock_guard<std::mutex> guard(lock_);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - boot_time_).count();
    const std::uint64_t current_mono = static_cast<std::uint64_t>(elapsed) + virtual_offset_us_;

    std::vector<std::uint32_t> expired;
    std::vector<std::uint32_t> to_remove;

    for (auto& [tid, desc] : timers_) {
        if (!desc.is_active) {
            to_remove.push_back(tid);
            continue;
        }

        if (current_mono >= desc.deadline_us) {
            expired.push_back(tid);
            if (desc.interval_us > 0) {
                // Periodic timer: advance deadline
                desc.deadline_us = current_mono + desc.interval_us;
            } else {
                // One-shot timer: mark inactive and remove
                desc.is_active = false;
                to_remove.push_back(tid);
            }
        }
    }

    for (auto tid : to_remove) {
        timers_.erase(tid);
    }

    return expired;
}

void VirtualClockService::advance_virtual_time(std::uint64_t delta_us) {
    std::lock_guard<std::mutex> guard(lock_);
    virtual_offset_us_ += delta_us;
}

bool VirtualClockService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    timers_.clear();
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualClockService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualClockService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        timers_.clear();
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualClockService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualClockService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualClockService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    boot_time_ = std::chrono::steady_clock::now();
    virtual_offset_us_ = 0;
    timers_.clear();
    next_timer_id_ = 1;
    state_ = DeviceState::Ready;
}

} // namespace winios::devices
