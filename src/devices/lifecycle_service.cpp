#include "devices/lifecycle_service.h"
#include "security/capability_manager.h"

#include <algorithm>

namespace winios::devices {

VirtualLifecycleService::VirtualLifecycleService()
    : VirtualDeviceBase(kDefaultLifecycleDeviceId,
                        1,
                        DeviceClass::Unknown,
                        "WinIOS Virtual Lifecycle & Power Service",
                        "WinIOS System Core",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::DeviceEnumerate)) {
    power_status_.battery_level = 100;
    power_status_.is_charging = 1;
    power_status_.is_low_power_mode = 0;
    power_status_.temperature_celsius = 32.0f;
    power_status_.system_power_state = 0;
}

PowerStatusData VirtualLifecycleService::get_power_status() const {
    std::lock_guard<std::mutex> guard(lock_);
    return power_status_;
}

void VirtualLifecycleService::set_battery_level(std::uint32_t level) {
    std::vector<EventListener> callbacks;
    SystemLifecycleEvent event_to_fire = SystemLifecycleEvent::Foreground;
    bool fire_event = false;

    {
        std::lock_guard<std::mutex> guard(lock_);
        power_status_.battery_level = std::clamp(level, 0u, 100u);
        if (power_status_.battery_level <= 10) {
            power_status_.system_power_state = 2; // Critical
            event_to_fire = SystemLifecycleEvent::LowBattery;
            fire_event = true;
        } else if (power_status_.battery_level <= 20) {
            power_status_.system_power_state = 1; // LowPower
            event_to_fire = SystemLifecycleEvent::LowBattery;
            fire_event = true;
        } else {
            power_status_.system_power_state = 0; // Normal
        }
        if (fire_event) {
            callbacks = listeners_;
            ++total_events_broadcast_;
        }
    }

    if (fire_event) {
        for (auto& cb : callbacks) {
            if (cb) cb(event_to_fire);
        }
    }
}

void VirtualLifecycleService::set_charging(bool charging) {
    std::lock_guard<std::mutex> guard(lock_);
    power_status_.is_charging = charging ? 1 : 0;
}

void VirtualLifecycleService::set_low_power_mode(bool enabled) {
    std::lock_guard<std::mutex> guard(lock_);
    power_status_.is_low_power_mode = enabled ? 1 : 0;
}

void VirtualLifecycleService::add_event_listener(EventListener listener) {
    std::lock_guard<std::mutex> guard(lock_);
    if (listener) {
        listeners_.push_back(std::move(listener));
    }
}

void VirtualLifecycleService::broadcast_event(SystemLifecycleEvent ev) {
    std::vector<EventListener> callbacks;
    {
        std::lock_guard<std::mutex> guard(lock_);
        callbacks = listeners_;
        ++total_events_broadcast_;
    }

    for (auto& cb : callbacks) {
        if (cb) cb(ev);
    }
}

std::uint64_t VirtualLifecycleService::total_events_broadcast() const {
    std::lock_guard<std::mutex> guard(lock_);
    return total_events_broadcast_;
}

bool VirtualLifecycleService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualLifecycleService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualLifecycleService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualLifecycleService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualLifecycleService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualLifecycleService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    power_status_.battery_level = 100;
    power_status_.is_charging = 1;
    power_status_.is_low_power_mode = 0;
    power_status_.temperature_celsius = 32.0f;
    power_status_.system_power_state = 0;
    listeners_.clear();
    total_events_broadcast_ = 0;
    state_ = DeviceState::Ready;
}

} // namespace winios::devices
