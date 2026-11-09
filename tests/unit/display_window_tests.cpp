#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ui/display_window.h"
#include "device/apple_display.h"
#include "runtime/darwin_boot_session.h"

#define UI_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::ui;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Window Geometry and Aspect Ratio Calculation
// -----------------------------------------------------------------------------
bool test_window_geometry_and_scaling() {
    std::cout << "[1] test_window_geometry_and_scaling...\n";

    DisplayWindowConfig config;
    config.native_width = 1179;
    config.native_height = 2556;
    config.scale = 0.5f;
    config.headless = true;

    DisplayWindow window(config);
    UI_ASSERT(window.config().native_width == 1179, "Native width must be 1179");
    UI_ASSERT(window.config().native_height == 2556, "Native height must be 2556");

    // Check scaled window dimensions: 1179 * 0.5 = 589, 2556 * 0.5 = 1278
    UI_ASSERT(window.window_width() == 589, "Window width should be 589");
    UI_ASSERT(window.window_height() == 1278, "Window height should be 1278");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Window Lifecycle (Create, Query, Close)
// -----------------------------------------------------------------------------
bool test_window_lifecycle() {
    std::cout << "[2] test_window_lifecycle...\n";

    DisplayWindowConfig config;
    config.headless = true;

    DisplayWindow window(config);
    UI_ASSERT(!window.is_open(), "Window should not be open before create()");

    UI_ASSERT(window.create(), "Window creation must succeed");
    UI_ASSERT(window.is_open(), "Window should be open after create()");
    UI_ASSERT(window.is_headless(), "Window must report headless mode");

    window.close();
    UI_ASSERT(!window.is_open(), "Window should be closed after close()");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Frame Presentation via AppleDisplay
// -----------------------------------------------------------------------------
bool test_frame_presentation() {
    std::cout << "[3] test_frame_presentation...\n";

    DisplayWindowConfig config;
    config.headless = true;
    DisplayWindow window(config);
    UI_ASSERT(window.create(), "Create window");

    AppleDisplay display(0x44495350u, 0x0B000000ULL, 0x10000ULL, 0x7C000000ULL);
    display.clear(0x00FF8800u); // Orange

    UI_ASSERT(window.total_frames_presented() == 0, "Frames should start at 0");

    UI_ASSERT(window.present_frame(display), "Present frame should succeed");
    UI_ASSERT(window.total_frames_presented() == 1, "Frame counter should increment to 1");

    // Present another frame
    display.clear(0x0000FF00u); // Green
    UI_ASSERT(window.present_frame(display), "Present second frame");
    UI_ASSERT(window.total_frames_presented() == 2, "Frame counter should increment to 2");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Boot Splash Presentation
// -----------------------------------------------------------------------------
bool test_boot_splash_presentation() {
    std::cout << "[4] test_boot_splash_presentation...\n";

    DisplayWindowConfig config;
    config.headless = true;
    DisplayWindow window(config);
    UI_ASSERT(window.create(), "Create window");

    AppleDisplay display;
    display.render_boot_splash();

    UI_ASSERT(window.present_frame(display), "Present boot splash frame");
    UI_ASSERT(window.total_frames_presented() == 1, "Frame count 1");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Touch Coordinate Translation & Clamping
// -----------------------------------------------------------------------------
bool test_touch_coordinate_translation() {
    std::cout << "[5] test_touch_coordinate_translation...\n";

    DisplayWindowConfig config;
    config.native_width = 1179;
    config.native_height = 2556;
    config.headless = true;
    DisplayWindow window(config);
    UI_ASSERT(window.create(), "Create window");

    // Inject touch in center
    window.inject_touch(589, 1278, true);
    auto touch = window.last_touch();
    UI_ASSERT(touch.x == 589, "Touch X should match injected coordinate");
    UI_ASSERT(touch.y == 1278, "Touch Y should match injected coordinate");
    UI_ASSERT(touch.is_down, "Touch must be active");

    // Release touch
    window.inject_touch(589, 1278, false);
    touch = window.last_touch();
    UI_ASSERT(!touch.is_down, "Touch should be released");

    // Test clamping beyond boundaries
    window.inject_touch(5000, 10000, true);
    touch = window.last_touch();
    UI_ASSERT(touch.x == 1178, "Clamped X must not exceed native_width - 1");
    UI_ASSERT(touch.y == 2555, "Clamped Y must not exceed native_height - 1");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Touch Callback Delivery
// -----------------------------------------------------------------------------
bool test_touch_callback() {
    std::cout << "[6] test_touch_callback...\n";

    DisplayWindowConfig config;
    config.headless = true;
    DisplayWindow window(config);
    UI_ASSERT(window.create(), "Create window");

    std::uint32_t received_x = 0;
    std::uint32_t received_y = 0;
    bool received_down = false;
    std::size_t callback_invocations = 0;

    window.set_touch_callback([&](const TouchEvent& ev) {
        received_x = ev.x;
        received_y = ev.y;
        received_down = ev.is_down;
        callback_invocations++;
    });

    window.inject_touch(200, 400, true);
    UI_ASSERT(callback_invocations == 1, "Callback should have fired once");
    UI_ASSERT(received_x == 200, "Received X match");
    UI_ASSERT(received_y == 400, "Received Y match");
    UI_ASSERT(received_down, "Received down state match");

    window.inject_touch(250, 450, false);
    UI_ASSERT(callback_invocations == 2, "Callback should have fired twice");
    UI_ASSERT(!received_down, "Received release state");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Screenshot BMP File Export and Validation
// -----------------------------------------------------------------------------
bool test_screenshot_bmp_export() {
    std::cout << "[7] test_screenshot_bmp_export...\n";

    DisplayWindowConfig config;
    config.native_width = 1179;
    config.native_height = 2556;
    config.headless = true;
    DisplayWindow window(config);
    UI_ASSERT(window.create(), "Create window");

    AppleDisplay display;
    display.render_boot_splash();
    window.present_frame(display);

    const std::string tmp_bmp = "test_iphone15_screenshot.bmp";
    if (std::filesystem::exists(tmp_bmp)) {
        std::filesystem::remove(tmp_bmp);
    }

    UI_ASSERT(window.save_screenshot_bmp(tmp_bmp), "Screenshot save must succeed");
    UI_ASSERT(std::filesystem::exists(tmp_bmp), "BMP file must exist on disk");

    // Read and validate BMP header
    std::ifstream in(tmp_bmp, std::ios::binary);
    UI_ASSERT(in.is_open(), "Open BMP file");

    char magic[2];
    in.read(magic, 2);
    UI_ASSERT(magic[0] == 'B' && magic[1] == 'M', "BMP magic must be 'BM'");

    std::uint32_t file_size = 0;
    in.read(reinterpret_cast<char*>(&file_size), 4);
    UI_ASSERT(file_size > 14 + 40, "File size must be larger than headers");

    in.seekg(18); // biWidth in BITMAPINFOHEADER
    std::uint32_t w = 0, h = 0;
    in.read(reinterpret_cast<char*>(&w), 4);
    in.read(reinterpret_cast<char*>(&h), 4);
    UI_ASSERT(w == 1179, "BMP width must be 1179");
    UI_ASSERT(h == 2556, "BMP height must be 2556");

    in.close();
    std::filesystem::remove(tmp_bmp);

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Integration with DarwinBootSession
// -----------------------------------------------------------------------------
bool test_darwin_boot_session_display_integration() {
    std::cout << "[8] test_darwin_boot_session_display_integration...\n";

    DarwinBootConfig boot_config;
    boot_config.ram_base = 0x40000000ULL;
    boot_config.ram_size = 0x08000000; // 128 MB

    DarwinBootSession session(boot_config);
    std::string err;
    UI_ASSERT(session.initialize(err), "Session init must succeed: " + err);

    DisplayWindowConfig win_config;
    win_config.headless = true;
    DisplayWindow window(win_config);
    UI_ASSERT(window.create(), "Window create");

    // Stream frame directly from session's AppleDisplay
    session.display().render_boot_splash();
    UI_ASSERT(window.present_frame(session.display()), "Stream session display frame");
    UI_ASSERT(window.total_frames_presented() == 1, "Total frames match");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== Running WinIOS iPhone 15 DirectX Display Window Tests ===\n";

    if (!test_window_geometry_and_scaling()) return 1;
    if (!test_window_lifecycle()) return 2;
    if (!test_frame_presentation()) return 3;
    if (!test_boot_splash_presentation()) return 4;
    if (!test_touch_coordinate_translation()) return 5;
    if (!test_touch_callback()) return 6;
    if (!test_screenshot_bmp_export()) return 7;
    if (!test_darwin_boot_session_display_integration()) return 8;

    std::cout << ">>> All 8 iPhone 15 DirectX Display Window Test Suites PASSED! <<<\n";
    return 0;
}
