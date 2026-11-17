// ============================================================================
// Stage 214.12: Guest UI Resource and Theme Management Tests
// ============================================================================
#include <cstdint>
#include <iostream>

#include "runtime/ui/theme.h"

using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define THEME_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[THEME_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [THEME_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_theme_and_palettes() {
    std::cout << "[Stage 214.12] Verifying Themes and Color Palettes...\n";

    auto& tm = ThemeManager::instance();

    // Dark mode verification
    tm.set_dark_mode(true);
    THEME_CHECK(tm.is_dark_mode(), "Dark mode enabled");
    THEME_CHECK(tm.palette().background == 0xFF000000, "Dark background is black");
    THEME_CHECK(tm.palette().text_primary == 0xFFFFFFFF, "Dark primary text is white");
    THEME_CHECK(tm.palette().primary == 0xFF007AFF, "Primary accent is iOS Blue");
    THEME_CHECK(tm.palette().success == 0xFF34C759, "Success accent is iOS Green");

    // Light mode verification
    tm.set_dark_mode(false);
    THEME_CHECK(!tm.is_dark_mode(), "Light mode enabled");
    THEME_CHECK(tm.palette().background == 0xFFFFFFFF, "Light background is white");
    THEME_CHECK(tm.palette().text_primary == 0xFF000000, "Light primary text is black");

    // Restore dark mode
    tm.set_dark_mode(true);

    // Metrics verification
    THEME_CHECK(tm.metrics().button_height == 44, "Default button height is 44");
    THEME_CHECK(tm.metrics().corner_radius == 10, "Default corner radius is 10");
    THEME_CHECK(tm.metrics().toggle_width == 51, "Default toggle width is 51");
}

void test_process_resource_isolation() {
    std::cout << "[Stage 214.12] Verifying Per-Process Resource Accounting and Cleanup...\n";

    auto& tm = ThemeManager::instance();

    const std::uint32_t pidA = 10;
    const std::uint32_t pidB = 20;

    // Allocate resources for pidA
    tm.track_resource(pidA, 101);
    tm.track_resource(pidA, 102);
    tm.track_resource(pidA, 103);
    THEME_CHECK(tm.active_resource_count_for_pid(pidA) == 3, "PID A has 3 active resources");

    // Allocate resources for pidB
    tm.track_resource(pidB, 201);
    tm.track_resource(pidB, 202);
    THEME_CHECK(tm.active_resource_count_for_pid(pidB) == 2, "PID B has 2 active resources");

    // Release resources for PID A on process termination
    tm.release_resources_for_pid(pidA);
    THEME_CHECK(tm.active_resource_count_for_pid(pidA) == 0, "PID A resources released cleanly");
    THEME_CHECK(tm.active_resource_count_for_pid(pidB) == 2, "PID B resources completely isolated and unaffected");

    // Release resources for PID B
    tm.release_resources_for_pid(pidB);
    THEME_CHECK(tm.active_resource_count_for_pid(pidB) == 0, "PID B resources released cleanly");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.12: UI Theme and Resource Tests   \n";
    std::cout << "====================================================\n";

    test_theme_and_palettes();
    test_process_resource_isolation();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL THEME TESTS PASSED" : "THEME FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
