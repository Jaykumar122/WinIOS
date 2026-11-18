#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/multi_window_compositor.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;

int main() {
    std::cout << "[Test Stage 226.4] Multi-Window Compositor & Damage Tracking...\n" << std::flush;

    DisplayConfiguration disp_cfg;
    disp_cfg.width = 800;
    disp_cfg.height = 600;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    MultiWindowCompositor comp(wm, disp);
    comp.set_background_color(0xFF112233);

    // 1. Create two overlapping windows with surfaces
    // Win 1: Red surface, at (100, 100, 300, 200)
    auto s1 = std::make_shared<DisplaySurface>(1, 1001, 300, 200);
    s1->fill_rect(1001, Rect{0, 0, 300, 200}, 0xFFFF0000); // ARGB Red
    uint64_t h1 = wm->create_window_v2(1001, "Red Window", Rect{100, 100, 300, 200}, s1);
    TEST_ASSERT(h1 != 0);

    // Win 2: Blue surface, at (200, 150, 300, 200) - overlaps Win 1
    auto s2 = std::make_shared<DisplaySurface>(2, 1002, 300, 200);
    s2->fill_rect(1002, Rect{0, 0, 300, 200}, 0xFF0000FF); // ARGB Blue
    uint64_t h2 = wm->create_window_v2(1002, "Blue Window", Rect{200, 150, 300, 200}, s2);
    TEST_ASSERT(h2 != 0);

    // Initial full composition
    TEST_ASSERT(comp.composite(true));
    TEST_ASSERT(comp.stats().total_frames >= 1);

    // Check background outside windows (e.g. at (50, 50))
    std::uint8_t* raw = disp->raw_data();
    std::uint32_t bg_pixel = *reinterpret_cast<std::uint32_t*>(raw + (50 * disp->stride()) + (50 * 4));
    TEST_ASSERT(bg_pixel == 0xFF112233);

    // Win 2 is on top of Win 1. In overlapping region (e.g. at (250, 200)), Blue should show.
    // Client frame of win 2: cf.x = 200 + 1, cf.y = 150 + 28
    // Let's sample inside client frame of win 2: (250, 200)
    std::uint32_t overlap_pixel = *reinterpret_cast<std::uint32_t*>(raw + (200 * disp->stride()) + (250 * 4));
    TEST_ASSERT(overlap_pixel == 0xFF0000FF);

    // In Win 1 only region (e.g. at (150, 150)), Red should show.
    std::uint32_t win1_pixel = *reinterpret_cast<std::uint32_t*>(raw + (150 * disp->stride()) + (150 * 4));
    TEST_ASSERT(win1_pixel == 0xFFFF0000);

    // 2. Bring Win 1 to front
    wm->raise_to_top(window_handle_id(h1));
    TEST_ASSERT(comp.composite(true));

    // Now in overlapping region (250, 200), Win 1 (Red) should show!
    overlap_pixel = *reinterpret_cast<std::uint32_t*>(raw + (200 * disp->stride()) + (250 * 4));
    TEST_ASSERT(overlap_pixel == 0xFFFF0000);

    // 3. Minimize Win 1
    TEST_ASSERT(wm->minimize_window(1001, h1));
    TEST_ASSERT(comp.composite(true));

    // Win 1 is minimized, so overlapping region (250, 200) should now be Blue (Win 2) again!
    overlap_pixel = *reinterpret_cast<std::uint32_t*>(raw + (200 * disp->stride()) + (250 * 4));
    TEST_ASSERT(overlap_pixel == 0xFF0000FF);

    // And region (150, 150) where Win 1 used to be should now be Background!
    win1_pixel = *reinterpret_cast<std::uint32_t*>(raw + (150 * disp->stride()) + (150 * 4));
    TEST_ASSERT(win1_pixel == 0xFF112233);

    // 4. Restore Win 1
    TEST_ASSERT(wm->restore_window(1001, h1));
    TEST_ASSERT(comp.composite(true));
    win1_pixel = *reinterpret_cast<std::uint32_t*>(raw + (150 * disp->stride()) + (150 * 4));
    TEST_ASSERT(win1_pixel == 0xFFFF0000);

    std::cout << "[Test Stage 226.4] PASSED successfully.\n" << std::flush;
    return 0;
}
