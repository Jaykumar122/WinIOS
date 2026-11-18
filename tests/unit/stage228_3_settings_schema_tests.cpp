#include "settings/settings_schema.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace winios::settings;

static void test_defaults() {
    SystemSettings s = SettingsSchema::create_default_settings();
    assert(s.schema_version == 1);
    assert(s.appearance.theme == ThemeMode::Dark);
    assert(s.appearance.accent == AccentColor::Blue);
    assert(!s.appearance.high_contrast);
    assert(s.display.width == 800);
    assert(s.display.height == 600);
    assert(s.display.scale == DisplayScale::Scale100);
    assert(s.audio.master_volume == 80);
    assert(!s.audio.is_muted);
    assert(s.power.battery_level_percent == 100);
    assert(!s.power.low_power_mode);
    assert(s.network.adapter_enabled);
    assert(s.network.loopback_only);
    assert(s.accessibility.text_scale_percent == 100);
    assert(!s.accessibility.high_contrast);
    assert(s.profile.user_id == "user_1000");
    std::cout << "[PASS] test_defaults\n";
}

static void test_validation() {
    SystemSettings valid = SettingsSchema::create_default_settings();
    assert(SettingsSchema::validate_system_settings(valid));

    SystemSettings invalid_ver = valid;
    invalid_ver.schema_version = 0;
    assert(!SettingsSchema::validate_system_settings(invalid_ver));

    SystemSettings invalid_scale = valid;
    invalid_scale.display.scale = static_cast<DisplayScale>(999);
    assert(!SettingsSchema::validate_system_settings(invalid_scale));

    SystemSettings invalid_vol = valid;
    invalid_vol.audio.master_volume = 150;
    assert(!SettingsSchema::validate_system_settings(invalid_vol));

    SystemSettings invalid_text_scale = valid;
    invalid_text_scale.accessibility.text_scale_percent = 50;
    assert(!SettingsSchema::validate_system_settings(invalid_text_scale));

    invalid_text_scale.accessibility.text_scale_percent = 250;
    assert(!SettingsSchema::validate_system_settings(invalid_text_scale));

    SystemSettings invalid_dim = valid;
    invalid_dim.display.width = 100;
    assert(!SettingsSchema::validate_system_settings(invalid_dim));

    std::cout << "[PASS] test_validation\n";
}

static void test_serialize_deserialize() {
    SystemSettings original = SettingsSchema::create_default_settings();
    original.appearance.theme = ThemeMode::Light;
    original.appearance.accent = AccentColor::Orange;
    original.appearance.high_contrast = true;
    original.display.width = 1024;
    original.display.height = 768;
    original.display.scale = DisplayScale::Scale125;
    original.display.orientation = DisplayOrientation::Portrait;
    original.audio.master_volume = 45;
    original.audio.is_muted = true;
    original.power.low_power_mode = true;
    original.accessibility.text_scale_percent = 150;
    original.profile.username = "alice_dev";

    std::string serialized = SettingsSchema::serialize(original);
    assert(!serialized.empty());
    assert(serialized.size() < SettingsSchema::kMaxSerializedSizeBytes);

    SystemSettings recovered{};
    bool ok = SettingsSchema::deserialize(serialized, recovered);
    assert(ok);
    (void)ok;
    assert(recovered.schema_version == original.schema_version);
    assert(recovered.appearance.theme == ThemeMode::Light);
    assert(recovered.appearance.accent == AccentColor::Orange);
    assert(recovered.appearance.high_contrast == true);
    assert(recovered.display.width == 1024);
    assert(recovered.display.height == 768);
    assert(recovered.display.scale == DisplayScale::Scale125);
    assert(recovered.display.orientation == DisplayOrientation::Portrait);
    assert(recovered.audio.master_volume == 45);
    assert(recovered.audio.is_muted == true);
    assert(recovered.power.low_power_mode == true);
    assert(recovered.accessibility.text_scale_percent == 150);
    assert(recovered.profile.username == "alice_dev");

    std::cout << "[PASS] test_serialize_deserialize\n";
}

static void test_malformed_ini() {
    SystemSettings out{};
    // Empty string
    assert(!SettingsSchema::deserialize("", out));

    // Non-INI garbage
    std::string garbage = "random text without any sections or keys\nmore garbage\n";
    assert(!SettingsSchema::deserialize(garbage, out));

    // Oversized buffer (> 32 KB)
    std::string huge(SettingsSchema::kMaxSerializedSizeBytes + 10, 'A');
    assert(!SettingsSchema::deserialize(huge, out));

    std::cout << "[PASS] test_malformed_ini\n";
}

int main() {
    std::cout << "--- Stage 228.3 Settings Schema Tests ---\n";
    test_defaults();
    test_validation();
    test_serialize_deserialize();
    test_malformed_ini();
    std::cout << "All Stage 228.3 Settings Schema Tests PASSED.\n";
    return 0;
}
