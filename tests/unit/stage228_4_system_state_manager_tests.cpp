#include "settings/system_state_manager.h"
#include <cassert>
#include <iostream>

using namespace winios::settings;

class MockObserver : public SystemStateObserver {
public:
    int appearance_calls{0};
    int display_calls{0};
    int audio_calls{0};
    int power_calls{0};
    int network_calls{0};
    int a11y_calls{0};
    int profile_calls{0};

    void on_appearance_changed(const AppearanceSettings&) override { appearance_calls++; }
    void on_display_changed(const DisplaySettings&) override { display_calls++; }
    void on_audio_changed(const AudioSettings&) override { audio_calls++; }
    void on_power_changed(const PowerSettings&) override { power_calls++; }
    void on_network_changed(const NetworkSettings&) override { network_calls++; }
    void on_accessibility_changed(const AccessibilitySettings&) override { a11y_calls++; }
    void on_profile_changed(const UserProfile&) override { profile_calls++; }
};

static void test_manager_basic_mutations() {
    SystemStateManager mgr;
    MockObserver obs;
    mgr.add_observer(&obs);

    assert(mgr.get_appearance().theme == ThemeMode::Dark);
    bool ok = mgr.set_theme(ThemeMode::Light);
    assert(ok);
    assert(mgr.get_appearance().theme == ThemeMode::Light);
    assert(obs.appearance_calls == 1);

    ok = mgr.set_accent(AccentColor::Green);
    assert(ok);
    assert(mgr.get_appearance().accent == AccentColor::Green);
    assert(obs.appearance_calls == 2);

    ok = mgr.set_display_scale(DisplayScale::Scale150);
    assert(ok);
    assert(mgr.get_display().scale == DisplayScale::Scale150);
    assert(obs.display_calls == 1);

    ok = mgr.set_audio_volume(65);
    assert(ok);
    assert(mgr.get_audio().master_volume == 65);
    assert(obs.audio_calls == 1);

    ok = mgr.set_audio_muted(true);
    assert(ok);
    assert(mgr.get_audio().is_muted);
    assert(obs.audio_calls == 2);

    ok = mgr.set_low_power_mode(true);
    assert(ok);
    assert(mgr.get_power().low_power_mode);
    assert(obs.power_calls == 1);

    ok = mgr.set_network_adapter_enabled(false);
    assert(ok);
    assert(!mgr.get_network().adapter_enabled);
    assert(obs.network_calls == 1);

    ok = mgr.set_text_scale(140);
    assert(ok);
    assert(mgr.get_accessibility().text_scale_percent == 140);
    assert(obs.a11y_calls == 1);

    ok = mgr.set_high_contrast(true);
    assert(ok);
    assert(mgr.get_accessibility().high_contrast);
    assert(obs.a11y_calls == 2);

    ok = mgr.switch_profile("user_2000", "bob", "Bob Smith");
    assert(ok);
    assert(mgr.get_profile().user_id == "user_2000");
    assert(obs.profile_calls == 1);

    mgr.remove_observer(&obs);
    std::cout << "[PASS] test_manager_basic_mutations\n";
}

static void test_manager_transactions_and_rollback() {
    SystemStateManager mgr;
    auto initial = mgr.get_settings();

    // Valid update
    bool ok = mgr.update_settings([](SystemSettings& s) {
        s.audio.master_volume = 50;
        s.appearance.theme = ThemeMode::Light;
    });
    assert(ok);
    assert(mgr.get_audio().master_volume == 50);
    assert(mgr.get_appearance().theme == ThemeMode::Light);

    // Invalid update (should rollback)
    ok = mgr.update_settings([](SystemSettings& s) {
        s.audio.master_volume = 999; // Invalid volume
        s.display.width = 10;        // Invalid width
    });
    assert(!ok);
    // Values should remain unchanged from before the failed transaction
    assert(mgr.get_audio().master_volume == 50);
    assert(mgr.get_appearance().theme == ThemeMode::Light);

    std::cout << "[PASS] test_manager_transactions_and_rollback\n";
}

static void test_manager_vfs_persistence() {
    auto vfs = std::make_shared<winios::vfs::VirtualFileSystem>();
    SystemStateManager mgr(vfs);

    mgr.set_theme(ThemeMode::Light);
    mgr.set_accent(AccentColor::Pink);
    mgr.set_audio_volume(90);
    mgr.set_low_power_mode(true);
    mgr.set_text_scale(125);

    // Save to VFS
    bool saved = mgr.save_to_vfs();
    assert(saved);
    (void)saved;
    assert(vfs->exists(SystemStateManager::kDefaultSettingsVfsPath));

    // Create a new manager using the same VFS and verify loading
    SystemStateManager loaded_mgr(vfs);
    bool loaded = loaded_mgr.load_from_vfs();
    assert(loaded);
    (void)loaded;
    assert(loaded_mgr.get_appearance().theme == ThemeMode::Light);
    assert(loaded_mgr.get_appearance().accent == AccentColor::Pink);
    assert(loaded_mgr.get_audio().master_volume == 90);
    assert(loaded_mgr.get_power().low_power_mode == true);
    assert(loaded_mgr.get_accessibility().text_scale_percent == 125);

    std::cout << "[PASS] test_manager_vfs_persistence\n";
}

int main() {
    std::cout << "--- Stage 228.4 System State Manager Tests ---\n";
    test_manager_basic_mutations();
    test_manager_transactions_and_rollback();
    test_manager_vfs_persistence();
    std::cout << "All Stage 228.4 System State Manager Tests PASSED.\n";
    return 0;
}
