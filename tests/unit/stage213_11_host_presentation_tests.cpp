// ============================================================================
// Stage 213.11 & 213.12: Windows Host Presentation & Frame Transfer Tests
// ============================================================================
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_server.h"
#include "ui/display_window.h"

using namespace winios::graphics;
using namespace winios::ui;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_directx_presentation_adapter() {
    std::cout << "[Test 1] Verify DirectXPresentationAdapter Frame Transfer & Damage Copying...\n";

    constexpr std::uint32_t kW = 100;
    constexpr std::uint32_t kH = 100;
    constexpr std::size_t kFbBytes = kW * kH * 4;

    DirectXPresentationAdapter adapter(kW, kH);
    TEST_CHECK(adapter.host_staging_buffer().size() == kFbBytes, "Host staging buffer allocated to 40,000 bytes");

    // Full frame presentation
    std::vector<std::uint8_t> fb(kFbBytes, 0x77);
    bool ok = adapter.present(fb.data(), fb.size(), Rect{0, 0, kW, kH});
    TEST_CHECK(ok, "Full frame present succeeded");
    TEST_CHECK(adapter.stats().presented_frames == 1, "Presented frames count is 1");
    TEST_CHECK(adapter.host_staging_buffer()[0] == 0x77, "Staging buffer content matches source");

    // Damage-optimized partial present: damage rect [20, 20, 10, 10]
    std::vector<std::uint8_t> fb2 = fb;
    for (std::uint32_t y = 20; y < 30; ++y) {
        for (std::uint32_t x = 20; x < 30; ++x) {
            fb2[(y * kW * 4) + (x * 4)] = 0xFF; // Blue byte
        }
    }
    ok = adapter.present(fb2.data(), fb2.size(), Rect{20, 20, 10, 10});
    TEST_CHECK(ok, "Partial damage present succeeded");
    TEST_CHECK(adapter.stats().presented_frames == 2, "Presented frames count is 2");
    TEST_CHECK(adapter.host_staging_buffer()[(25 * kW * 4) + (25 * 4)] == 0xFF, "Damaged pixel transferred");
    TEST_CHECK(adapter.host_staging_buffer()[0] == 0x77, "Undamaged pixel preserved");

    // Frame drop when buffer undersized
    ok = adapter.present(fb.data(), 100, Rect{0, 0, kW, kH});
    TEST_CHECK(!ok, "Undersized buffer rejected");
    TEST_CHECK(adapter.stats().dropped_frames == 1, "Dropped frames count incremented to 1");
}

void test_display_window_headless_mode() {
    std::cout << "[Test 2] Verify DisplayWindow Headless Mode, Presentation & Screenshot...\n";

    DisplayWindowConfig config{};
    config.native_width = 200;
    config.native_height = 400;
    config.scale = 0.5f;
    config.headless = true; // Automated test mode

    DisplayWindow win(config);
    TEST_CHECK(win.create(), "Headless DisplayWindow create succeeded");
    TEST_CHECK(win.is_open(), "DisplayWindow reports open");
    TEST_CHECK(win.is_headless(), "DisplayWindow reports headless");

    // Message pump in headless mode
    TEST_CHECK(win.pump_events(), "pump_events succeeds in headless mode");

    // Present frames
    std::vector<std::uint8_t> frame(200 * 400 * 4, 0);
    // Draw a test gradient pattern
    for (std::uint32_t y = 0; y < 400; ++y) {
        for (std::uint32_t x = 0; x < 200; ++x) {
            std::size_t idx = (y * 200 * 4) + (x * 4);
            frame[idx + 0] = static_cast<std::uint8_t>(x);       // B
            frame[idx + 1] = static_cast<std::uint8_t>(y / 2);   // G
            frame[idx + 2] = 200;                               // R
            frame[idx + 3] = 255;                               // A
        }
    }

    TEST_CHECK(win.present_frame(frame.data(), 200, 400), "present_frame succeeds");
    TEST_CHECK(win.total_frames_presented() == 1, "Total frames presented is 1");

    // Save screenshot BMP
    std::string bmp_dir = "artifacts/campaign-213";
    if (std::filesystem::exists("WinIOS/artifacts") || std::filesystem::exists("WinIOS")) {
        bmp_dir = "WinIOS/artifacts/campaign-213";
    }
    std::filesystem::create_directories(bmp_dir);
    const std::string bmp_path = bmp_dir + "/test_host_presentation.bmp";
    bool bmp_ok = win.save_screenshot_bmp(bmp_path);
    TEST_CHECK(bmp_ok, "save_screenshot_bmp succeeded");

    // Verify BMP file exists and has non-zero size
    std::ifstream bmp_file(bmp_path, std::ios::binary);
    TEST_CHECK(bmp_file.is_open(), "Screenshot BMP file exists on disk");
    if (bmp_file.is_open()) {
        char magic[2];
        bmp_file.read(magic, 2);
        TEST_CHECK(magic[0] == 'B' && magic[1] == 'M', "BMP magic header 'BM' is valid");
    }

    win.close();
    TEST_CHECK(!win.is_open(), "DisplayWindow closed cleanly");
}

void test_touch_injection_and_callback() {
    std::cout << "[Test 3] Verify Touch Input Injection and Event Callback...\n";

    DisplayWindowConfig config{};
    config.headless = true;
    DisplayWindow win(config);
    win.create();

    TouchEvent received_touch{};
    bool callback_called = false;

    win.set_touch_callback([&](const TouchEvent& evt) {
        received_touch = evt;
        callback_called = true;
    });

    win.inject_touch(350, 750, true);
    TEST_CHECK(callback_called, "Touch callback invoked");
    TEST_CHECK(received_touch.x == 350 && received_touch.y == 750, "Touch coordinates match (350, 750)");
    TEST_CHECK(received_touch.is_down, "Touch down state is true");

    TouchEvent last = win.last_touch();
    TEST_CHECK(last.x == 350 && last.y == 750 && last.is_down, "last_touch matches injected touch");

    win.close();
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.11: Host Presentation Tests       \n";
    std::cout << "====================================================\n";

    test_directx_presentation_adapter();
    test_display_window_headless_mode();
    test_touch_injection_and_callback();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
