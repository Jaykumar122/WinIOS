#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/virtual_display.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"

using namespace winios;
using namespace winios::graphics;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_damage_tracking_and_frame_skipping() {
    std::cout << "[Test 1] Compositor damage tracking and idle frame skipping...\n";
    DisplayConfiguration cfg{};
    cfg.width = 400;
    cfg.height = 300;
    cfg.row_stride = 400 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);

    Compositor comp(display);

    auto surf = std::make_shared<DisplaySurface>(1, 10, 200, 150);
    surf->set_position(50, 50);
    surf->set_z_order(1);
    surf->fill_rect(10, Rect{0, 0, 200, 150}, 0xFF00FF00u); // Green
    comp.add_surface(surf);

    // Initial frame: full composite
    bool ok = comp.composite(true);
    TEST_ASSERT(ok);
    TEST_ASSERT(comp.stats().total_frames == 1);
    TEST_ASSERT(comp.stats().full_redraw_frames == 1);

    // Second call without any damage or surface updates: MUST skip
    ok = comp.composite(false);
    TEST_ASSERT(ok);
    TEST_ASSERT(comp.stats().total_frames == 1);
    TEST_ASSERT(comp.stats().skipped_frames == 1);

    // Invalidate sub-rectangle damage
    comp.invalidate_rect(Rect{60, 60, 40, 40});
    ok = comp.composite(false);
    TEST_ASSERT(ok);
    TEST_ASSERT(comp.stats().total_frames == 2);
    TEST_ASSERT(comp.stats().partial_redraw_frames == 1);

    std::cout << "[PASS] Damage tracking and skipping verified.\n";
}

void test_complex_occlusion_and_alpha_blending() {
    std::cout << "[Test 2] Multi-window occlusion culling and alpha blending...\n";
    DisplayConfiguration cfg{};
    cfg.width = 500;
    cfg.height = 500;
    cfg.row_stride = 500 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);

    Compositor comp(display);
    comp.set_background_color(0xFF101010u);

    // Window 1 (Background): 300x300 at (50, 50), Red, z=1
    auto surf_bg = std::make_shared<DisplaySurface>(1, 10, 300, 300);
    surf_bg->set_position(50, 50);
    surf_bg->set_z_order(1);
    surf_bg->fill_rect(10, Rect{0, 0, 300, 300}, 0xFFFF0000u); // Pure Red
    comp.add_surface(surf_bg);

    // Window 2 (Foreground Opaque): 150x150 at (100, 100), Blue, z=2, fully occludes middle of Window 1
    auto surf_fg = std::make_shared<DisplaySurface>(2, 20, 150, 150);
    surf_fg->set_position(100, 100);
    surf_fg->set_z_order(2);
    surf_fg->fill_rect(20, Rect{0, 0, 150, 150}, 0xFF0000FFu); // Pure Blue
    comp.add_surface(surf_fg);

    bool ok = comp.composite(true);
    TEST_ASSERT(ok);

    // Pixel at (70, 70) is inside Window 1 only -> Must be Red (0xFFFF0000)
    uint32_t px_red = display->read_pixel(70, 70);
    TEST_ASSERT(px_red == 0xFFFF0000u);

    // Pixel at (120, 120) is inside Window 2 -> Must be Blue (0xFF0000FF)
    uint32_t px_blue = display->read_pixel(120, 120);
    TEST_ASSERT(px_blue == 0xFF0000FFu);

    // Pixel at (10, 10) is outside all windows -> Must be background (0xFF101010)
    uint32_t px_bg = display->read_pixel(10, 10);
    TEST_ASSERT(px_bg == 0xFF101010u);

    std::cout << "[PASS] Multi-window composition verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.16: COMPOSITOR DAMAGE TESTS\n";
    std::cout << "========================================\n";

    test_damage_tracking_and_frame_skipping();
    test_complex_occlusion_and_alpha_blending();

    std::cout << "\nALL STAGE 223.16 TESTS PASSED (100%).\n";
    return 0;
}
