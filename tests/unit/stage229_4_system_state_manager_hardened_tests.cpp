#include "settings/system_state_manager.h"
#include "platform/platform_error.h"
#include "vfs/vfs.h"
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

#include <iostream>
#include <vector>

using namespace winios::settings;
using namespace winios::platform;

class ReentrantObserver : public SystemStateObserver {
public:
    explicit ReentrantObserver(SystemStateManager* mgr) : mgr_(mgr) {}

    void on_appearance_changed(const AppearanceSettings&) override {
        call_count++;
        if (call_count == 1 && mgr_) {
            // Reentrantly remove ourselves and add a sibling observer
            mgr_->remove_observer(this);
            if (sibling_) {
                mgr_->add_observer(sibling_);
            }
        }
    }

    SystemStateManager* mgr_{nullptr};
    SystemStateObserver* sibling_{nullptr};
    int call_count{0};
};

class SiblingObserver : public SystemStateObserver {
public:
    void on_appearance_changed(const AppearanceSettings&) override {
        sibling_calls++;
    }
    int sibling_calls{0};
};

static void test_version_counter_and_stale_update() {
    SystemStateManager mgr;
    TEST_ASSERT(mgr.get_version() == 1);

    bool ok = mgr.set_theme(ThemeMode::Light);
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr.get_version() == 2);

    // Update with correct expected version
    ok = mgr.update_settings_versioned(2, [](SystemSettings& s) {
        s.audio.master_volume = 40;
    });
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr.get_version() == 3);
    TEST_ASSERT(mgr.get_audio().master_volume == 40);

    // Update with stale expected version (expected 2, but current is 3) -> rejected!
    ok = mgr.update_settings_versioned(2, [](SystemSettings& s) {
        s.audio.master_volume = 90;
    });
    TEST_ASSERT(!ok); // Stale conflict rejected
    TEST_ASSERT(mgr.get_version() == 3);
    TEST_ASSERT(mgr.get_audio().master_volume == 40); // Unchanged

    std::cout << "[PASS] test_version_counter_and_stale_update\n";
}

static void test_multiple_updates_in_one_transaction() {
    SystemStateManager mgr;
    uint64_t v0 = mgr.get_version();

    std::vector<std::function<void(SystemSettings&)>> batch = {
        [](SystemSettings& s) { s.appearance.theme = ThemeMode::Light; },
        [](SystemSettings& s) { s.appearance.accent = AccentColor::Orange; },
        [](SystemSettings& s) { s.audio.master_volume = 72; },
        [](SystemSettings& s) { s.accessibility.text_scale_percent = 135; }
    };

    bool ok = mgr.execute_transaction(batch);
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr.get_version() == v0 + 1);
    TEST_ASSERT(mgr.get_appearance().theme == ThemeMode::Light);
    TEST_ASSERT(mgr.get_appearance().accent == AccentColor::Orange);
    TEST_ASSERT(mgr.get_audio().master_volume == 72);
    TEST_ASSERT(mgr.get_accessibility().text_scale_percent == 135);

    // Batch with one invalid step rolls back entire batch
    uint64_t v1 = mgr.get_version();
    std::vector<std::function<void(SystemSettings&)>> bad_batch = {
        [](SystemSettings& s) { s.audio.master_volume = 10; },
        [](SystemSettings& s) { s.audio.master_volume = 999; } // Invalid! Bound is 100
    };
    ok = mgr.execute_transaction(bad_batch);
    TEST_ASSERT(!ok); // Entire batch rejected
    TEST_ASSERT(mgr.get_version() == v1);
    TEST_ASSERT(mgr.get_audio().master_volume == 72); // Rolled back

    std::cout << "[PASS] test_multiple_updates_in_one_transaction\n";
}

static void test_persistence_failure_rollback() {
    auto vfs = std::make_shared<winios::vfs::VirtualFileSystem>();
    SystemStateManager mgr(vfs);
    mgr.save_to_vfs();

    uint64_t v0 = mgr.get_version();
    TEST_ASSERT(mgr.get_audio().master_volume == 80);

    // Enable persistence fault injection
    mgr.set_persistence_fault_injection(true);

    bool ok = mgr.set_audio_volume(25);
    TEST_ASSERT(!ok); // Persistence failed -> transaction rolled back!
    TEST_ASSERT(mgr.get_version() == v0);
    TEST_ASSERT(mgr.get_audio().master_volume == 80); // Memory state kept intact

    // Disable fault injection -> succeeds
    mgr.set_persistence_fault_injection(false);
    ok = mgr.set_audio_volume(25);
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr.get_version() == v0 + 1);
    TEST_ASSERT(mgr.get_audio().master_volume == 25);

    std::cout << "[PASS] test_persistence_failure_rollback\n";
}

static void test_reentrant_observer_add_and_remove() {
    SystemStateManager mgr;
    SiblingObserver sibling;
    ReentrantObserver reentrant(&mgr);
    reentrant.sibling_ = &sibling;

    mgr.add_observer(&reentrant);

    // Trigger appearance change: reentrant will remove itself and add sibling
    bool ok = mgr.set_theme(ThemeMode::Light);
    TEST_ASSERT(ok);
    TEST_ASSERT(reentrant.call_count == 1);

    // Trigger second appearance change: reentrant is no longer registered, sibling receives it!
    ok = mgr.set_theme(ThemeMode::Dark);
    TEST_ASSERT(ok);
    TEST_ASSERT(reentrant.call_count == 1); // Not called again
    TEST_ASSERT(sibling.sibling_calls == 1);

    mgr.remove_observer(&sibling);
    std::cout << "[PASS] test_reentrant_observer_add_and_remove\n";
}

static void test_crash_safe_persistence_atomic_replacement() {
    auto vfs = std::make_shared<winios::vfs::VirtualFileSystem>();
    SystemStateManager mgr(vfs);

    mgr.set_theme(ThemeMode::Light);
    mgr.set_accent(AccentColor::Red);
    TEST_ASSERT(vfs->exists(SystemStateManager::kDefaultSettingsVfsPath));

    // Inspect VFS: temp files (.tmp.*) must not remain after successful atomic rename
    std::vector<winios::vfs::VfsDirEntry> entries;
    vfs->readdir("/System/Library/Preferences", entries);
    for (const auto& e : entries) {
        TEST_ASSERT(e.name.find(".tmp.") == std::string::npos);
    }

    // Verify backup file exists
    TEST_ASSERT(vfs->exists(std::string(SystemStateManager::kDefaultSettingsVfsPath) + ".bak"));

    // Verify deserialization matches
    SystemStateManager mgr2(vfs);
    bool ok = mgr2.load_from_vfs();
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr2.get_appearance().theme == ThemeMode::Light);
    TEST_ASSERT(mgr2.get_appearance().accent == AccentColor::Red);

    std::cout << "[PASS] test_crash_safe_persistence_atomic_replacement\n";
}

static void test_corrupt_file_recovery_from_backup() {
    auto vfs = std::make_shared<winios::vfs::VirtualFileSystem>();
    SystemStateManager mgr(vfs);
    mgr.set_theme(ThemeMode::Light);
    mgr.set_accent(AccentColor::Purple);
    mgr.set_audio_volume(42);
    // Rotate the state containing volume=42 into .bak via an additional mutation
    mgr.set_low_power_mode(true);

    const std::string path = SystemStateManager::kDefaultSettingsVfsPath;
    const std::string bak = path + ".bak";
    TEST_ASSERT(vfs->exists(path));
    TEST_ASSERT(vfs->exists(bak));

    // Corrupt primary file with garbage
    const char garbage[] = "CORRUPTED_GARBAGE_DATA_12345!@#$%^&*()";
    vfs->write_file(path, garbage, sizeof(garbage));

    // Load in a fresh manager -> should recover from backup
    SystemStateManager mgr2(vfs);
    bool ok = mgr2.load_from_vfs(path);
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr2.was_recovered_from_backup());
    TEST_ASSERT(mgr2.get_appearance().theme == ThemeMode::Light);
    TEST_ASSERT(mgr2.get_appearance().accent == AccentColor::Purple);
    TEST_ASSERT(mgr2.get_audio().master_volume == 42);

    std::cout << "[PASS] test_corrupt_file_recovery_from_backup\n";
}

static void test_corrupt_file_recovery_to_clean_defaults() {
    auto vfs = std::make_shared<winios::vfs::VirtualFileSystem>();
    const std::string path = SystemStateManager::kDefaultSettingsVfsPath;
    const std::string bak = path + ".bak";

    vfs->mkdir("/System");
    vfs->mkdir("/System/Library");
    vfs->mkdir("/System/Library/Preferences");

    // Both primary and backup are corrupt
    const char garbage[] = "COMPLETELY_UNREADABLE";
    vfs->write_file(path, garbage, sizeof(garbage));
    vfs->write_file(bak, garbage, sizeof(garbage));

    SystemStateManager mgr(vfs);
    bool ok = mgr.load_from_vfs(path);
    TEST_ASSERT(ok);
    TEST_ASSERT(mgr.was_recovered_from_backup());
    // Recovered to valid defaults
    TEST_ASSERT(mgr.get_appearance().theme == ThemeMode::Dark);
    TEST_ASSERT(mgr.get_audio().master_volume == 80);

    std::cout << "[PASS] test_corrupt_file_recovery_to_clean_defaults\n";
}

int main() {
    std::cout << "=== Running Stage 229 SystemStateManager Hardened Tests ===\n";
    test_version_counter_and_stale_update();
    test_multiple_updates_in_one_transaction();
    test_persistence_failure_rollback();
    test_reentrant_observer_add_and_remove();
    test_crash_safe_persistence_atomic_replacement();
    test_corrupt_file_recovery_from_backup();
    test_corrupt_file_recovery_to_clean_defaults();
    std::cout << "All Stage 229 SystemStateManager Hardened Tests Passed!\n";
    return 0;
}
