#pragma once

#include "settings/settings_types.h"
#include "settings/settings_schema.h"
#include "vfs/vfs.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winios::settings {

class SystemStateObserver {
public:
    virtual ~SystemStateObserver() = default;
    virtual void on_appearance_changed(const AppearanceSettings&) {}
    virtual void on_display_changed(const DisplaySettings&) {}
    virtual void on_audio_changed(const AudioSettings&) {}
    virtual void on_input_changed(const InputSettings&) {}
    virtual void on_power_changed(const PowerSettings&) {}
    virtual void on_network_changed(const NetworkSettings&) {}
    virtual void on_accessibility_changed(const AccessibilitySettings&) {}
    virtual void on_profile_changed(const UserProfile&) {}
};

class SystemStateManager {
public:
    static constexpr const char* kDefaultSettingsVfsPath = "/System/Library/Preferences/system_settings.ini";

    explicit SystemStateManager(std::shared_ptr<vfs::VirtualFileSystem> vfs = nullptr);
    ~SystemStateManager() = default;

    SystemStateManager(const SystemStateManager&) = delete;
    SystemStateManager& operator=(const SystemStateManager&) = delete;

    // Direct Read Queries
    [[nodiscard]] SystemSettings get_settings() const;
    [[nodiscard]] AppearanceSettings get_appearance() const;
    [[nodiscard]] DisplaySettings get_display() const;
    [[nodiscard]] AudioSettings get_audio() const;
    [[nodiscard]] InputSettings get_input() const;
    [[nodiscard]] PowerSettings get_power() const;
    [[nodiscard]] NetworkSettings get_network() const;
    [[nodiscard]] AccessibilitySettings get_accessibility() const;
    [[nodiscard]] UserProfile get_profile() const;

    // Transactional Mutators
    bool update_settings(const std::function<void(SystemSettings&)>& mutator);
    bool set_theme(ThemeMode theme);
    bool set_accent(AccentColor accent);
    bool set_display_scale(DisplayScale scale);
    bool set_display_orientation(DisplayOrientation orientation);
    bool set_audio_volume(std::uint32_t volume);
    bool set_audio_muted(bool muted);
    bool set_low_power_mode(bool enabled);
    bool set_network_adapter_enabled(bool enabled);
    bool set_text_scale(std::uint32_t percent);
    bool set_high_contrast(bool enabled);
    bool switch_profile(const std::string& user_id, const std::string& username, const std::string& display_name);

    // Observer registration
    void add_observer(SystemStateObserver* obs);
    void remove_observer(SystemStateObserver* obs);

    // Persistence to / from VFS
    bool save_to_vfs(const std::string& vfs_path = kDefaultSettingsVfsPath);
    bool load_from_vfs(const std::string& vfs_path = kDefaultSettingsVfsPath);

    void reset_to_defaults();

private:
    void notify_observers_locked(const SystemSettings& prev, const SystemSettings& curr);

    mutable std::recursive_mutex            lock_;
    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    SystemSettings                          settings_;
    std::vector<SystemStateObserver*>       observers_;
};

} // namespace winios::settings
