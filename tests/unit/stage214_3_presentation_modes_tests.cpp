// ============================================================================
// Stage 214.3: Headless and Actual Windows Presentation Modes Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "ui/display_window.h"

using namespace winios::ui;

namespace {

int g_failures = 0;

#define MODE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[MODE_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [MODE_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_headless_mode() {
    std::cout << "[Stage 214.3] Verifying Headless Presentation Mode...\n";

    DisplayWindowConfig cfg{};
    cfg.title = "WinIOS Headless Test";
    cfg.native_width = 800;
    cfg.native_height = 1200;
    cfg.headless = true;

    DisplayWindow win(cfg);
    MODE_CHECK(win.is_headless(), "Window is in headless mode");
    MODE_CHECK(win.create(), "Headless window created successfully");
    MODE_CHECK(win.is_open(), "Headless window reports open");

    // Generate test pattern (800x1200 RGBA, 3840000 bytes)
    std::vector<std::uint8_t> pixels(800 * 1200 * 4, 0x7F);
    // Write distinct marker at (10, 10)
    reinterpret_cast<std::uint32_t*>(pixels.data())[10 * 800 + 10] = 0xFF34C759;

    MODE_CHECK(win.present_frame(pixels.data(), 800, 1200), "Present frame in headless mode succeeds");
    MODE_CHECK(win.total_frames_presented() == 1, "Total frames presented is 1");
    MODE_CHECK(win.backbuffer().size() == 800 * 1200 * 4, "Backbuffer size matches native resolution");
    MODE_CHECK(reinterpret_cast<const std::uint32_t*>(win.backbuffer().data())[10 * 800 + 10] == 0xFF34C759,
               "Backbuffer pixel value exactly preserved");

    // Test touch injection in headless mode
    bool touch_received = false;
    win.set_touch_callback([&](const TouchEvent& ev) {
        if (ev.x == 150 && ev.y == 250 && ev.is_down) {
            touch_received = true;
        }
    });

    win.inject_touch(150, 250, true);
    MODE_CHECK(touch_received, "Touch event received via callback in headless mode");
    MODE_CHECK(win.last_touch().x == 150 && win.last_touch().y == 250, "Last touch state recorded");

    // Pump events in headless mode returns true without hanging
    MODE_CHECK(win.pump_events(), "Pump events in headless mode succeeds");

    win.close();
    MODE_CHECK(!win.is_open(), "Headless window closed cleanly");
}

void test_desktop_window_mode() {
    std::cout << "[Stage 214.3] Verifying Desktop Presentation Mode...\n";

    DisplayWindowConfig cfg{};
    cfg.title = "WinIOS Desktop Window Test";
    cfg.native_width = 800;
    cfg.native_height = 1200;
    cfg.scale = 0.5f; // 400 x 600 footprint
    cfg.headless = false; // Real Windows window requested

    DisplayWindow win(cfg);
    MODE_CHECK(!win.is_headless(), "Window configured for desktop mode");
    MODE_CHECK(win.window_width() == 400, "Window width scaled to 400");
    MODE_CHECK(win.window_height() == 600, "Window height scaled to 600");

    bool created = win.create();
    MODE_CHECK(created, "Desktop window create() succeeded (or safely fell back to headless in non-GUI runner)");
    MODE_CHECK(win.is_open(), "Desktop window is open");

    std::vector<std::uint8_t> pixels(800 * 1200 * 4, 0xAA);
    MODE_CHECK(win.present_frame(pixels.data(), 800, 1200), "Present frame in desktop mode succeeds");
    MODE_CHECK(win.total_frames_presented() == 1, "Total frames presented counter incremented");

    win.pump_events();

    win.close();
    MODE_CHECK(!win.is_open(), "Desktop window closed cleanly");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.3: Presentation Modes Tests       \n";
    std::cout << "====================================================\n";

    test_headless_mode();
    test_desktop_window_mode();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PRESENTATION MODE TESTS PASSED" : "MODE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
