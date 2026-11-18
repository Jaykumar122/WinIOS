#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace winios::devices {

enum class SystemLifecycleEvent : std::uint32_t {
    Foreground     = 1,
    Background     = 2,
    Suspend        = 3,
    Resume         = 4,
    LowMemory      = 5,
    LowBattery     = 6,
    SystemShutdown = 7
};

inline const char* lifecycle_event_to_string(SystemLifecycleEvent ev) noexcept {
    switch (ev) {
    case SystemLifecycleEvent::Foreground:     return "Foreground";
    case SystemLifecycleEvent::Background:     return "Background";
    case SystemLifecycleEvent::Suspend:        return "Suspend";
    case SystemLifecycleEvent::Resume:         return "Resume";
    case SystemLifecycleEvent::LowMemory:      return "LowMemory";
    case SystemLifecycleEvent::LowBattery:     return "LowBattery";
    case SystemLifecycleEvent::SystemShutdown: return "SystemShutdown";
    default:                                  return "UnknownEvent";
    }
}

#pragma pack(push, 8)
struct PowerStatusData {
    std::uint32_t battery_level{100};    // 0 - 100%
    std::uint8_t  is_charging{1};        // 1 = charging, 0 = discharging
    std::uint8_t  is_low_power_mode{0};  // 1 = low power mode enabled
    std::uint8_t  pad[2]{0};
    float         temperature_celsius{32.0f};
    std::uint32_t system_power_state{0}; // 0 = Normal, 1 = LowPower, 2 = Critical
};
#pragma pack(pop)

class VirtualLifecycleService : public VirtualDeviceBase {
public:
    using EventListener = std::function<void(SystemLifecycleEvent ev)>;

    static constexpr std::uint32_t kDefaultLifecycleDeviceId = 10;

    VirtualLifecycleService();
    ~VirtualLifecycleService() override = default;

    // Power queries & updates
    [[nodiscard]] PowerStatusData get_power_status() const;
    void set_battery_level(std::uint32_t level);
    void set_charging(bool charging);
    void set_low_power_mode(bool enabled);

    // Event registration & broadcast
    void add_event_listener(EventListener listener);
    void broadcast_event(SystemLifecycleEvent ev);

    // Metrics
    [[nodiscard]] std::uint64_t total_events_broadcast() const;

    // Lifecycle
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    mutable std::mutex lock_;
    PowerStatusData    power_status_{};
    std::vector<EventListener> listeners_;
    std::uint64_t      total_events_broadcast_{0};
};

} // namespace winios::devices
