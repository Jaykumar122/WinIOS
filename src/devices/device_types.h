#pragma once

#include <cstdint>
#include <string>

namespace winios::devices {

// -----------------------------------------------------------------------------
// Device Class Enumeration
// -----------------------------------------------------------------------------
enum class DeviceClass : std::uint32_t {
    Unknown       = 0,
    Display       = 1,
    PointerInput  = 2,
    KeyboardInput = 3,
    AudioOutput   = 4,
    AudioInput    = 5,
    Sensor        = 6,
    Network       = 7,
    Clock         = 8,
    Notification  = 9
};

inline const char* device_class_to_string(DeviceClass cls) noexcept {
    switch (cls) {
    case DeviceClass::Display:       return "Display";
    case DeviceClass::PointerInput:  return "PointerInput";
    case DeviceClass::KeyboardInput: return "KeyboardInput";
    case DeviceClass::AudioOutput:   return "AudioOutput";
    case DeviceClass::AudioInput:    return "AudioInput";
    case DeviceClass::Sensor:        return "Sensor";
    case DeviceClass::Network:       return "Network";
    case DeviceClass::Clock:         return "Clock";
    case DeviceClass::Notification:  return "Notification";
    default:                         return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// Device State Lifecycle Enumeration
// -----------------------------------------------------------------------------
enum class DeviceState : std::uint32_t {
    Uninitialized = 0,
    Ready         = 1,
    Running       = 2,
    Suspended     = 3,
    Stopped       = 4,
    Error         = 5,
    Removed       = 6
};

inline const char* device_state_to_string(DeviceState state) noexcept {
    switch (state) {
    case DeviceState::Uninitialized: return "Uninitialized";
    case DeviceState::Ready:         return "Ready";
    case DeviceState::Running:       return "Running";
    case DeviceState::Suspended:     return "Suspended";
    case DeviceState::Stopped:       return "Stopped";
    case DeviceState::Error:         return "Error";
    case DeviceState::Removed:       return "Removed";
    default:                         return "UnknownState";
    }
}

// -----------------------------------------------------------------------------
// Guest-Visible Device Information Descriptor
// -----------------------------------------------------------------------------
#pragma pack(push, 8)
struct DeviceInfo {
    std::uint32_t device_id{0};
    std::uint32_t generation{1};
    DeviceClass   device_class{DeviceClass::Unknown};
    DeviceState   state{DeviceState::Uninitialized};
    char          name[64]{0};
    char          provider[32]{0};
    std::uint32_t version{0x00010000};
    std::uint32_t required_capability{0};
    std::uint32_t flags{0};
    std::uint64_t handle{0};
};
#pragma pack(pop)

// Handle helpers: 64-bit generation-tagged handles: (gen << 32) | id
inline constexpr std::uint64_t make_device_handle(std::uint32_t id, std::uint32_t gen) noexcept {
    return (static_cast<std::uint64_t>(gen) << 32) | static_cast<std::uint64_t>(id);
}

inline constexpr std::uint32_t device_handle_id(std::uint64_t handle) noexcept {
    return static_cast<std::uint32_t>(handle & 0xFFFFFFFF);
}

inline constexpr std::uint32_t device_handle_gen(std::uint64_t handle) noexcept {
    return static_cast<std::uint32_t>(handle >> 32);
}

} // namespace winios::devices
