// ============================================================================
// Stage 213.19: Guest UI Runtime Library & Typography Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "graphics/virtual_display.h"
#include "runtime/guest_ui_runtime.h"

using namespace winios::graphics;
using namespace winios::runtime;

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

void test_ascii_glyph_and_string_rendering() {
    std::cout << "[Test 1] Verify Built-in 8x8 ASCII Font Glyphs and String Rendering...\n";

    constexpr std::uint32_t kW = 200;
    constexpr std::uint32_t kH = 100;
    std::vector<std::uint8_t> buffer(kW * kH * 4, 0);

    // Draw char 'A' at (10, 10) with solid white color (0xFFFFFFFF)
    GuestUiRuntime::draw_char_8x8(
        buffer.data(), kW, kH, kW * 4,
        PixelFormat::B8G8R8A8_UNORM,
        10, 10, 'A', 0xFFFFFFFFu, 1);

    // Verify non-zero pixels exist in the 8x8 cell
    std::uint32_t lit_pixels = 0;
    for (std::uint32_t y = 10; y < 18; ++y) {
        for (std::uint32_t x = 10; x < 18; ++x) {
            std::size_t offset = (y * kW * 4) + (x * 4);
            std::uint32_t col = 0;
            std::memcpy(&col, buffer.data() + offset, 4);
            if (col == 0xFFFFFFFFu) {
                lit_pixels++;
            }
        }
    }
    TEST_CHECK(lit_pixels > 0, "Glyph 'A' rendered non-zero lit pixels");

    // Draw string "Hello 2026!" with scale 2
    GuestUiRuntime::draw_string_8x8(
        buffer.data(), kW, kH, kW * 4,
        PixelFormat::B8G8R8A8_UNORM,
        30, 30, "Hello 2026!", 0xFF00FF00u, 2);

    // Check pixel at (30, 30) area
    bool has_green = false;
    for (std::uint32_t y = 30; y < 50; ++y) {
        for (std::uint32_t x = 30; x < 180; ++x) {
            std::size_t offset = (y * kW * 4) + (x * 4);
            std::uint32_t col = 0;
            std::memcpy(&col, buffer.data() + offset, 4);
            if (col == 0xFF00FF00u) {
                has_green = true;
                break;
            }
        }
        if (has_green) break;
    }
    TEST_CHECK(has_green, "String 'Hello 2026!' rendered with scale 2 in green");
}

void test_button_and_icon_components() {
    std::cout << "[Test 2] Verify Interactive Button and App Icon Component Rendering...\n";

    constexpr std::uint32_t kW = 300;
    constexpr std::uint32_t kH = 300;
    std::vector<std::uint8_t> buffer(kW * kH * 4, 0);

    // Draw Button
    Rect btn_frame{20, 20, 160, 50};
    GuestUiRuntime::draw_button(
        buffer.data(), kW, kH, kW * 4,
        PixelFormat::B8G8R8A8_UNORM,
        btn_frame, "Click Me", 0xFF0066CCu, 0xFFFFFFFFu, false);

    // Check center pixel of button has bg color
    std::size_t btn_center = (45 * kW * 4) + (100 * 4);
    std::uint32_t center_col = 0;
    std::memcpy(&center_col, buffer.data() + btn_center, 4);
    TEST_CHECK(center_col != 0, "Button interior rendered with non-zero color");

    // Draw App Icon
    Rect icon_frame{50, 100, 80, 80};
    GuestUiRuntime::draw_app_icon(
        buffer.data(), kW, kH, kW * 4,
        PixelFormat::B8G8R8A8_UNORM,
        icon_frame, "Notes", 0xFFFF9500u);

    std::size_t icon_center = (140 * kW * 4) + (90 * 4);
    std::uint32_t icon_col = 0;
    std::memcpy(&icon_col, buffer.data() + icon_center, 4);
    TEST_CHECK(icon_col == 0xFFFF9500u, "App icon body rendered with 0xFFFF9500");
}

void test_status_bar_rendering() {
    std::cout << "[Test 3] Verify Status Bar and Dynamic Island Notch Rendering...\n";

    constexpr std::uint32_t kW = 400;
    constexpr std::uint32_t kH = 600;
    std::vector<std::uint8_t> buffer(kW * kH * 4, 0);

    GuestUiRuntime::draw_status_bar(
        buffer.data(), kW, kH, kW * 4,
        PixelFormat::B8G8R8A8_UNORM,
        "10:42", 95);

    // Check top status bar banner is dark
    std::uint32_t banner_col = 0;
    std::memcpy(&banner_col, buffer.data(), 4);
    TEST_CHECK(banner_col == 0xFF050508u, "Status bar background rendered with dark banner");

    // Check Dynamic Island notch center is pure black
    std::size_t notch_center = (25 * kW * 4) + (200 * 4);
    std::uint32_t notch_col = 0;
    std::memcpy(&notch_col, buffer.data() + notch_center, 4);
    TEST_CHECK(notch_col == 0xFF000000u, "Dynamic Island notch pill rendered in center");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.19: Guest UI Runtime Tests        \n";
    std::cout << "====================================================\n";

    test_ascii_glyph_and_string_rendering();
    test_button_and_icon_components();
    test_status_bar_rendering();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
