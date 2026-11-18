#include "settings/settings_schema.h"

#include <sstream>
#include <algorithm>

namespace winios::settings {

bool SettingsSchema::validate_appearance(const AppearanceSettings& s) {
    if (static_cast<std::uint32_t>(s.theme) > static_cast<std::uint32_t>(ThemeMode::System)) return false;
    if (static_cast<std::uint32_t>(s.accent) > static_cast<std::uint32_t>(AccentColor::Graphite)) return false;
    return true;
}

bool SettingsSchema::validate_display(const DisplaySettings& s) {
    if (s.width < 320 || s.width > 3840) return false;
    if (s.height < 240 || s.height > 2160) return false;
    if (s.scale != DisplayScale::Scale100 && s.scale != DisplayScale::Scale125 &&
        s.scale != DisplayScale::Scale150 && s.scale != DisplayScale::Scale200) {
        return false;
    }
    if (static_cast<std::uint32_t>(s.orientation) > static_cast<std::uint32_t>(DisplayOrientation::Portrait)) return false;
    if (s.refresh_rate_hz != 60 && s.refresh_rate_hz != 120) return false;
    return true;
}

bool SettingsSchema::validate_audio(const AudioSettings& s) {
    if (s.master_volume > 100) return false;
    if (s.output_device.empty() || s.output_device.size() > 64) return false;
    return true;
}

bool SettingsSchema::validate_input(const InputSettings& s) {
    if (s.key_repeat_delay_ms < 100 || s.key_repeat_delay_ms > 1000) return false;
    if (s.key_repeat_rate_ms < 10 || s.key_repeat_rate_ms > 200) return false;
    if (s.pointer_sensitivity < 1 || s.pointer_sensitivity > 10) return false;
    return true;
}

bool SettingsSchema::validate_power(const PowerSettings& s) {
    if (s.battery_level_percent > 100) return false;
    if (s.screen_idle_timeout_sec < 30 || s.screen_idle_timeout_sec > 3600) return false;
    return true;
}

bool SettingsSchema::validate_network(const NetworkSettings& s) {
    // In research profile, network must remain loopback isolated
    if (!s.loopback_only) return false;
    return true;
}

bool SettingsSchema::validate_accessibility(const AccessibilitySettings& s) {
    if (s.text_scale_percent < 100 || s.text_scale_percent > 200) return false;
    return true;
}

bool SettingsSchema::validate_system_settings(const SystemSettings& s) {
    if (s.schema_version == 0 || s.schema_version > kCurrentSchemaVersion) return false;
    if (!validate_appearance(s.appearance)) return false;
    if (!validate_display(s.display)) return false;
    if (!validate_audio(s.audio)) return false;
    if (!validate_input(s.input)) return false;
    if (!validate_power(s.power)) return false;
    if (!validate_network(s.network)) return false;
    if (!validate_accessibility(s.accessibility)) return false;
    if (s.profile.user_id.empty() || s.profile.username.empty()) return false;
    return true;
}

SystemSettings SettingsSchema::create_default_settings() {
    SystemSettings s{};
    s.schema_version = kCurrentSchemaVersion;
    s.appearance = AppearanceSettings{};
    s.display = DisplaySettings{};
    s.audio = AudioSettings{};
    s.input = InputSettings{};
    s.power = PowerSettings{};
    s.network = NetworkSettings{};
    s.accessibility = AccessibilitySettings{};
    s.profile = UserProfile{};
    return s;
}

std::string SettingsSchema::serialize(const SystemSettings& s) {
    std::ostringstream ss;
    ss << "schema_version=" << s.schema_version << "\n";

    // [appearance]
    ss << "[appearance]\n";
    ss << "theme=" << static_cast<std::uint32_t>(s.appearance.theme) << "\n";
    ss << "accent=" << static_cast<std::uint32_t>(s.appearance.accent) << "\n";
    ss << "high_contrast=" << (s.appearance.high_contrast ? 1 : 0) << "\n";
    ss << "reduce_transparency=" << (s.appearance.reduce_transparency ? 1 : 0) << "\n";

    // [display]
    ss << "[display]\n";
    ss << "width=" << s.display.width << "\n";
    ss << "height=" << s.display.height << "\n";
    ss << "scale=" << static_cast<std::uint32_t>(s.display.scale) << "\n";
    ss << "orientation=" << static_cast<std::uint32_t>(s.display.orientation) << "\n";
    ss << "refresh_rate=" << s.display.refresh_rate_hz << "\n";
    ss << "auto_brightness=" << (s.display.auto_brightness ? 1 : 0) << "\n";

    // [audio]
    ss << "[audio]\n";
    ss << "master_volume=" << s.audio.master_volume << "\n";
    ss << "is_muted=" << (s.audio.is_muted ? 1 : 0) << "\n";
    ss << "output_device=" << s.audio.output_device << "\n";
    ss << "sound_effects=" << (s.audio.sound_effects_enabled ? 1 : 0) << "\n";

    // [input]
    ss << "[input]\n";
    ss << "repeat_delay=" << s.input.key_repeat_delay_ms << "\n";
    ss << "repeat_rate=" << s.input.key_repeat_rate_ms << "\n";
    ss << "sensitivity=" << s.input.pointer_sensitivity << "\n";
    ss << "pointer_visible=" << (s.input.pointer_visible ? 1 : 0) << "\n";
    ss << "primary_left=" << (s.input.primary_button_left ? 1 : 0) << "\n";

    // [power]
    ss << "[power]\n";
    ss << "battery_level=" << s.power.battery_level_percent << "\n";
    ss << "is_charging=" << (s.power.is_charging ? 1 : 0) << "\n";
    ss << "low_power_mode=" << (s.power.low_power_mode ? 1 : 0) << "\n";
    ss << "screen_timeout=" << s.power.screen_idle_timeout_sec << "\n";

    // [network]
    ss << "[network]\n";
    ss << "adapter_enabled=" << (s.network.adapter_enabled ? 1 : 0) << "\n";
    ss << "loopback_only=" << (s.network.loopback_only ? 1 : 0) << "\n";

    // [accessibility]
    ss << "[accessibility]\n";
    ss << "text_scale=" << s.accessibility.text_scale_percent << "\n";
    ss << "high_contrast=" << (s.accessibility.high_contrast ? 1 : 0) << "\n";
    ss << "reduce_motion=" << (s.accessibility.reduce_motion ? 1 : 0) << "\n";
    ss << "focus_indicator=" << (s.accessibility.focus_indicator_visible ? 1 : 0) << "\n";

    // [profile]
    ss << "[profile]\n";
    ss << "user_id=" << s.profile.user_id << "\n";
    ss << "username=" << s.profile.username << "\n";
    ss << "display_name=" << s.profile.display_name << "\n";

    return ss.str();
}

bool SettingsSchema::deserialize(const std::string& data, SystemSettings& out_s) {
    if (data.empty() || data.size() > kMaxSerializedSizeBytes) return false;

    SystemSettings s = create_default_settings();
    s.schema_version = 0; // Require explicit version marker

    std::istringstream ss(data);
    std::string line;
    std::string current_section;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (current_section.empty()) {
            if (key == "schema_version") {
                try { s.schema_version = std::stoul(val); } catch (...) {}
            }
            continue;
        }

        if (current_section == "appearance") {
            if (key == "theme") {
                try { s.appearance.theme = static_cast<ThemeMode>(std::stoul(val)); } catch (...) {}
            } else if (key == "accent") {
                try { s.appearance.accent = static_cast<AccentColor>(std::stoul(val)); } catch (...) {}
            } else if (key == "high_contrast") {
                s.appearance.high_contrast = (val == "1" || val == "true");
            } else if (key == "reduce_transparency") {
                s.appearance.reduce_transparency = (val == "1" || val == "true");
            }
        } else if (current_section == "display") {
            if (key == "width") {
                try { s.display.width = std::stoul(val); } catch (...) {}
            } else if (key == "height") {
                try { s.display.height = std::stoul(val); } catch (...) {}
            } else if (key == "scale") {
                try { s.display.scale = static_cast<DisplayScale>(std::stoul(val)); } catch (...) {}
            } else if (key == "orientation") {
                try { s.display.orientation = static_cast<DisplayOrientation>(std::stoul(val)); } catch (...) {}
            } else if (key == "refresh_rate") {
                try { s.display.refresh_rate_hz = std::stoul(val); } catch (...) {}
            } else if (key == "auto_brightness") {
                s.display.auto_brightness = (val == "1" || val == "true");
            }
        } else if (current_section == "audio") {
            if (key == "master_volume") {
                try { s.audio.master_volume = std::stoul(val); } catch (...) {}
            } else if (key == "is_muted") {
                s.audio.is_muted = (val == "1" || val == "true");
            } else if (key == "output_device") {
                s.audio.output_device = val;
            } else if (key == "sound_effects") {
                s.audio.sound_effects_enabled = (val == "1" || val == "true");
            }
        } else if (current_section == "input") {
            if (key == "repeat_delay") {
                try { s.input.key_repeat_delay_ms = std::stoul(val); } catch (...) {}
            } else if (key == "repeat_rate") {
                try { s.input.key_repeat_rate_ms = std::stoul(val); } catch (...) {}
            } else if (key == "sensitivity") {
                try { s.input.pointer_sensitivity = std::stoul(val); } catch (...) {}
            } else if (key == "pointer_visible") {
                s.input.pointer_visible = (val == "1" || val == "true");
            } else if (key == "primary_left") {
                s.input.primary_button_left = (val == "1" || val == "true");
            }
        } else if (current_section == "power") {
            if (key == "battery_level") {
                try { s.power.battery_level_percent = std::stoul(val); } catch (...) {}
            } else if (key == "is_charging") {
                s.power.is_charging = (val == "1" || val == "true");
            } else if (key == "low_power_mode") {
                s.power.low_power_mode = (val == "1" || val == "true");
            } else if (key == "screen_timeout") {
                try { s.power.screen_idle_timeout_sec = std::stoul(val); } catch (...) {}
            }
        } else if (current_section == "network") {
            if (key == "adapter_enabled") {
                s.network.adapter_enabled = (val == "1" || val == "true");
            } else if (key == "loopback_only") {
                s.network.loopback_only = (val == "1" || val == "true");
            }
        } else if (current_section == "accessibility") {
            if (key == "text_scale") {
                try { s.accessibility.text_scale_percent = std::stoul(val); } catch (...) {}
            } else if (key == "high_contrast") {
                s.accessibility.high_contrast = (val == "1" || val == "true");
            } else if (key == "reduce_motion") {
                s.accessibility.reduce_motion = (val == "1" || val == "true");
            } else if (key == "focus_indicator") {
                s.accessibility.focus_indicator_visible = (val == "1" || val == "true");
            }
        } else if (current_section == "profile") {
            if (key == "user_id") s.profile.user_id = val;
            else if (key == "username") s.profile.username = val;
            else if (key == "display_name") s.profile.display_name = val;
        }
    }

    if (s.schema_version == 0) return false;

    // Validate deserialized fields
    if (!validate_system_settings(s)) return false;

    out_s = s;
    return true;
}

bool SettingsSchema::migrate(SystemSettings& s, std::uint32_t from_version, std::uint32_t to_version) {
    if (from_version == to_version) return true;
    if (from_version == 1 && to_version == 2) {
        // Example migration: ensure safe defaults
        s.schema_version = 2;
        return true;
    }
    return false;
}

} // namespace winios::settings
