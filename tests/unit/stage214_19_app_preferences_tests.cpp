// ============================================================================
// Stage 214.19: Application Preferences and Persistent State Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "kernel/app_preferences.h"

using namespace winios::kernel;

namespace {

int g_failures = 0;

#define PREF_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[PREF_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [PREF_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_typed_preferences_and_isolation() {
    std::cout << "[Stage 214.19] Verifying Typed Preferences and Per-App Isolation...\n";

    auto& pm = AppPreferencesManager::instance();
    pm.reset();

    const std::string counter_app = "com.winios.counter";
    const std::string notes_app = "com.winios.notes";

    // Counter app saves integer count and boolean flag
    pm.set_int(counter_app, "current_count", 42);
    pm.set_bool(counter_app, "vibration_enabled", true);

    // Notes app saves string note and float font size
    pm.set_string(notes_app, "note_title", "Shopping List");
    pm.set_float(notes_app, "font_size", 14.5);

    // Verify counter app can read its values
    auto c_val = pm.get_int(counter_app, "current_count");
    PREF_CHECK(c_val.has_value() && *c_val == 42, "Counter app read count == 42");
    auto c_vib = pm.get_bool(counter_app, "vibration_enabled");
    PREF_CHECK(c_vib.has_value() && *c_vib == true, "Counter app read vibration == true");

    // Verify cross-application isolation: Counter CANNOT read Notes values
    PREF_CHECK(!pm.has_key(counter_app, "note_title"), "Counter app cannot see note_title");
    PREF_CHECK(!pm.get_string(counter_app, "note_title").has_value(), "note_title returns nullopt for Counter");

    // Verify Notes app can read its values
    auto n_title = pm.get_string(notes_app, "note_title");
    PREF_CHECK(n_title.has_value() && *n_title == "Shopping List", "Notes app read note_title");
    auto n_font = pm.get_float(notes_app, "font_size");
    PREF_CHECK(n_font.has_value() && *n_font == 14.5, "Notes app read font_size == 14.5");

    // Verify cross-application isolation: Notes CANNOT read Counter values
    PREF_CHECK(!pm.has_key(notes_app, "current_count"), "Notes app cannot see current_count");
}

void test_preferences_serialization_and_snapshot() {
    std::cout << "[Stage 214.19] Verifying Snapshot Serialization & Replay...\n";

    auto& pm = AppPreferencesManager::instance();
    pm.reset();

    pm.set_int("com.winios.counter", "saved_count", 108);
    pm.set_string("com.winios.notes", "cached_text", "Snapshot persistence test");

    // Serialize
    std::vector<std::uint8_t> snapshot;
    pm.serialize(snapshot);
    PREF_CHECK(!snapshot.empty(), "Preferences serialized to snapshot buffer");

    // Clear manager
    pm.reset();
    PREF_CHECK(!pm.has_key("com.winios.counter", "saved_count"), "Manager cleared");

    // Deserialize
    const std::uint8_t* ptr = snapshot.data();
    std::size_t remaining = snapshot.size();
    bool restored = pm.deserialize(ptr, remaining);
    PREF_CHECK(restored, "Preferences restored from snapshot");

    // Assert restored values
    auto count_opt = pm.get_int("com.winios.counter", "saved_count");
    PREF_CHECK(count_opt.has_value() && *count_opt == 108, "Restored saved_count == 108");
    auto text_opt = pm.get_string("com.winios.notes", "cached_text");
    PREF_CHECK(text_opt.has_value() && *text_opt == "Snapshot persistence test", "Restored cached_text");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.19: App Preferences Tests         \n";
    std::cout << "====================================================\n";

    test_typed_preferences_and_isolation();
    test_preferences_serialization_and_snapshot();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PREFERENCES TESTS PASSED" : "PREFERENCES FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
