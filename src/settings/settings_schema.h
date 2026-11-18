#pragma once

#include "settings/settings_types.h"

#include <string>
#include <vector>

namespace winios::settings {

class SettingsSchema {
public:
    static constexpr std::uint32_t kCurrentSchemaVersion = 1;
    static constexpr std::size_t   kMaxSerializedSizeBytes = 32 * 1024; // 32 KB bound

    // Validation functions
    static bool validate_appearance(const AppearanceSettings& s);
    static bool validate_display(const DisplaySettings& s);
    static bool validate_audio(const AudioSettings& s);
    static bool validate_input(const InputSettings& s);
    static bool validate_power(const PowerSettings& s);
    static bool validate_network(const NetworkSettings& s);
    static bool validate_accessibility(const AccessibilitySettings& s);
    static bool validate_system_settings(const SystemSettings& s);

    // Default factory
    static SystemSettings create_default_settings();

    // Serialization & Deserialization
    static std::string serialize(const SystemSettings& s);
    static bool deserialize(const std::string& data, SystemSettings& out_s);

    // Migration
    static bool migrate(SystemSettings& s, std::uint32_t from_version, std::uint32_t to_version);
};

} // namespace winios::settings
