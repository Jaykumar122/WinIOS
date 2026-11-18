#include "settings/system_state_manager.h"

#include <algorithm>

namespace winios::settings {

SystemStateManager::SystemStateManager(std::shared_ptr<vfs::VirtualFileSystem> vfs)
    : vfs_(std::move(vfs)), settings_(SettingsSchema::create_default_settings()) {}

SystemSettings SystemStateManager::get_settings() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_;
}

AppearanceSettings SystemStateManager::get_appearance() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.appearance;
}

DisplaySettings SystemStateManager::get_display() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.display;
}

AudioSettings SystemStateManager::get_audio() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.audio;
}

InputSettings SystemStateManager::get_input() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.input;
}

PowerSettings SystemStateManager::get_power() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.power;
}

NetworkSettings SystemStateManager::get_network() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.network;
}

AccessibilitySettings SystemStateManager::get_accessibility() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.accessibility;
}

UserProfile SystemStateManager::get_profile() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return settings_.profile;
}

bool SystemStateManager::update_settings(const std::function<void(SystemSettings&)>& mutator) {
    if (!mutator) return false;

    std::lock_guard<std::recursive_mutex> guard(lock_);
    SystemSettings tentative = settings_;
    mutator(tentative);

    if (!SettingsSchema::validate_system_settings(tentative)) {
        return false; // Validation failure, abort transaction
    }

    SystemSettings prev = settings_;
    settings_ = tentative;

    notify_observers_locked(prev, settings_);
    save_to_vfs();
    return true;
}

bool SystemStateManager::set_theme(ThemeMode theme) {
    return update_settings([theme](SystemSettings& s) {
        s.appearance.theme = theme;
    });
}

bool SystemStateManager::set_accent(AccentColor accent) {
    return update_settings([accent](SystemSettings& s) {
        s.appearance.accent = accent;
    });
}

bool SystemStateManager::set_display_scale(DisplayScale scale) {
    return update_settings([scale](SystemSettings& s) {
        s.display.scale = scale;
    });
}

bool SystemStateManager::set_display_orientation(DisplayOrientation orientation) {
    return update_settings([orientation](SystemSettings& s) {
        s.display.orientation = orientation;
    });
}

bool SystemStateManager::set_audio_volume(std::uint32_t volume) {
    return update_settings([volume](SystemSettings& s) {
        s.audio.master_volume = std::min(volume, 100u);
    });
}

bool SystemStateManager::set_audio_muted(bool muted) {
    return update_settings([muted](SystemSettings& s) {
        s.audio.is_muted = muted;
    });
}

bool SystemStateManager::set_low_power_mode(bool enabled) {
    return update_settings([enabled](SystemSettings& s) {
        s.power.low_power_mode = enabled;
    });
}

bool SystemStateManager::set_network_adapter_enabled(bool enabled) {
    return update_settings([enabled](SystemSettings& s) {
        s.network.adapter_enabled = enabled;
    });
}

bool SystemStateManager::set_text_scale(std::uint32_t percent) {
    return update_settings([percent](SystemSettings& s) {
        s.accessibility.text_scale_percent = std::clamp(percent, 100u, 200u);
    });
}

bool SystemStateManager::set_high_contrast(bool enabled) {
    return update_settings([enabled](SystemSettings& s) {
        s.appearance.high_contrast = enabled;
        s.accessibility.high_contrast = enabled;
    });
}

bool SystemStateManager::switch_profile(const std::string& user_id, const std::string& username, const std::string& display_name) {
    if (user_id.empty() || username.empty()) return false;
    return update_settings([&](SystemSettings& s) {
        s.profile.user_id = user_id;
        s.profile.username = username;
        s.profile.display_name = display_name.empty() ? username : display_name;
    });
}

void SystemStateManager::add_observer(SystemStateObserver* obs) {
    if (!obs) return;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (std::find(observers_.begin(), observers_.end(), obs) == observers_.end()) {
        observers_.push_back(obs);
    }
}

void SystemStateManager::remove_observer(SystemStateObserver* obs) {
    if (!obs) return;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(), obs), observers_.end());
}

void SystemStateManager::notify_observers_locked(const SystemSettings& prev, const SystemSettings& curr) {
    for (auto* obs : observers_) {
        if (!obs) continue;
        if (prev.appearance.theme != curr.appearance.theme ||
            prev.appearance.accent != curr.appearance.accent ||
            prev.appearance.high_contrast != curr.appearance.high_contrast ||
            prev.appearance.reduce_transparency != curr.appearance.reduce_transparency) {
            obs->on_appearance_changed(curr.appearance);
        }
        if (prev.display.width != curr.display.width ||
            prev.display.height != curr.display.height ||
            prev.display.scale != curr.display.scale ||
            prev.display.orientation != curr.display.orientation ||
            prev.display.refresh_rate_hz != curr.display.refresh_rate_hz) {
            obs->on_display_changed(curr.display);
        }
        if (prev.audio.master_volume != curr.audio.master_volume ||
            prev.audio.is_muted != curr.audio.is_muted) {
            obs->on_audio_changed(curr.audio);
        }
        if (prev.input.pointer_sensitivity != curr.input.pointer_sensitivity ||
            prev.input.primary_button_left != curr.input.primary_button_left) {
            obs->on_input_changed(curr.input);
        }
        if (prev.power.low_power_mode != curr.power.low_power_mode ||
            prev.power.battery_level_percent != curr.power.battery_level_percent) {
            obs->on_power_changed(curr.power);
        }
        if (prev.network.adapter_enabled != curr.network.adapter_enabled) {
            obs->on_network_changed(curr.network);
        }
        if (prev.accessibility.text_scale_percent != curr.accessibility.text_scale_percent ||
            prev.accessibility.high_contrast != curr.accessibility.high_contrast ||
            prev.accessibility.reduce_motion != curr.accessibility.reduce_motion) {
            obs->on_accessibility_changed(curr.accessibility);
        }
        if (prev.profile.user_id != curr.profile.user_id) {
            obs->on_profile_changed(curr.profile);
        }
    }
}

bool SystemStateManager::save_to_vfs(const std::string& vfs_path) {
    if (!vfs_ || vfs_path.empty()) return false;
    std::lock_guard<std::recursive_mutex> guard(lock_);

    const std::string serialized = SettingsSchema::serialize(settings_);
    vfs_->mkdir("/System");
    vfs_->mkdir("/System/Library");
    vfs_->mkdir("/System/Library/Preferences");

    const std::string tmp_path = vfs_path + ".tmp";
    if (!vfs_->write_file(tmp_path, serialized.data(), serialized.size())) {
        return false;
    }

    if (!vfs_->write_file(vfs_path, serialized.data(), serialized.size())) {
        vfs_->unlink(tmp_path);
        return false;
    }

    vfs_->unlink(tmp_path);
    return true;
}

bool SystemStateManager::load_from_vfs(const std::string& vfs_path) {
    if (!vfs_ || vfs_path.empty()) return false;
    std::lock_guard<std::recursive_mutex> guard(lock_);

    std::vector<std::uint8_t> data;
    if (!vfs_->read_file(vfs_path, data) || data.empty()) {
        return false;
    }

    const std::string str(data.begin(), data.end());
    SystemSettings loaded{};
    if (!SettingsSchema::deserialize(str, loaded)) {
        return false;
    }

    SystemSettings prev = settings_;
    settings_ = loaded;
    notify_observers_locked(prev, settings_);
    return true;
}

void SystemStateManager::reset_to_defaults() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    SystemSettings prev = settings_;
    settings_ = SettingsSchema::create_default_settings();
    notify_observers_locked(prev, settings_);
    save_to_vfs();
}

} // namespace winios::settings
