#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winios::settings {

enum class ThemeMode : std::uint32_t {
    Light  = 0,
    Dark   = 1,
    System = 2
};

enum class AccentColor : std::uint32_t {
    Blue     = 0,
    Purple   = 1,
    Pink     = 2,
    Red      = 3,
    Orange   = 4,
    Yellow   = 5,
    Green    = 6,
    Graphite = 7
};

enum class DisplayOrientation : std::uint32_t {
    Landscape = 0,
    Portrait  = 1
};

enum class DisplayScale : std::uint32_t {
    Scale100 = 100,
    Scale125 = 125,
    Scale150 = 150,
    Scale200 = 200
};

enum class TaskState : std::uint32_t {
    Running   = 0,
    Suspended = 1,
    Zombie    = 2,
    Crashed   = 3
};

struct AppearanceSettings {
    ThemeMode   theme{ThemeMode::Dark};
    AccentColor accent{AccentColor::Blue};
    bool        high_contrast{false};
    bool        reduce_transparency{false};
};

struct DisplaySettings {
    std::uint32_t      width{800};
    std::uint32_t      height{600};
    DisplayScale       scale{DisplayScale::Scale100};
    DisplayOrientation orientation{DisplayOrientation::Landscape};
    std::uint32_t      refresh_rate_hz{60};
    bool               auto_brightness{false};
};

struct AudioSettings {
    std::uint32_t master_volume{80}; // 0..100
    bool          is_muted{false};
    std::string   output_device{"Virtual Speakers (Stereo)"};
    bool          sound_effects_enabled{true};
};

struct InputSettings {
    std::uint32_t key_repeat_delay_ms{250};
    std::uint32_t key_repeat_rate_ms{30};
    std::uint32_t pointer_sensitivity{5}; // 1..10
    bool          pointer_visible{true};
    bool          primary_button_left{true};
};

struct PowerSettings {
    std::uint32_t battery_level_percent{100}; // 0..100
    bool          is_charging{true};
    bool          low_power_mode{false};
    std::uint32_t screen_idle_timeout_sec{300};
};

struct NetworkSettings {
    bool          adapter_enabled{true};
    bool          loopback_only{true}; // Always true in research isolation
    std::uint64_t bytes_sent{0};
    std::uint64_t bytes_received{0};
};

struct AccessibilitySettings {
    std::uint32_t text_scale_percent{100}; // 100..200
    bool          high_contrast{false};
    bool          reduce_motion{false};
    bool          focus_indicator_visible{true};
    bool          screen_reader_active{false};
};

struct TaskInfo {
    std::uint32_t pid{0};
    std::string   bundle_id;
    std::string   name;
    std::uint64_t primary_window{0};
    TaskState     state{TaskState::Running};
    std::uint64_t launch_time_us{0};
    std::uint64_t memory_bytes{0};
    bool          is_foreground{false};
};

struct UserProfile {
    std::string user_id{"user_1000"};
    std::string username{"default_user"};
    std::string display_name{"Default User"};
    bool        is_active{true};
};

struct SystemSettings {
    std::uint32_t         schema_version{1};
    AppearanceSettings    appearance;
    DisplaySettings       display;
    AudioSettings         audio;
    InputSettings         input;
    PowerSettings         power;
    NetworkSettings       network;
    AccessibilitySettings accessibility;
    UserProfile           profile;
};

} // namespace winios::settings
