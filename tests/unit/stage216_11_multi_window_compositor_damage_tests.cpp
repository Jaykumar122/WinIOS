#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "graphics/multi_window_compositor.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios::graphics;

void test_compositor_background_and_window_blit() {
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 600;
    disp_cfg.height = 400;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    MultiWindowCompositor comp(wm, disp);
    comp.set_background_color(0xFF10141E);

    auto s1 = std::make_shared<DisplaySurface>(1, 10, 200, 100);
    s1->fill_rect(10, Rect{0, 0, 200, 100}, 0xFF00FF00); // Green client area

    std::uint32_t w1 = wm->create_window(10, "Green App", Rect{50, 50, 202, 129}, s1);
    TEST_ASSERT(w1 > 0);

    TEST_ASSERT(comp.composite(true)); // Force full redraw
    TEST_ASSERT(comp.stats().total_frames == 1);
    TEST_ASSERT(comp.stats().full_redraw_frames == 1);

    // Verify background pixel outside window (e.g. at 10, 10)
    std::uint8_t* raw = disp->raw_data();
    std::uint32_t bg_pixel = *reinterpret_cast<std::uint32_t*>(raw + (10 * disp->stride()) + (10 * 4));
    TEST_ASSERT(bg_pixel == 0xFF10141E);

    // Verify client pixel inside window (e.g. at 60, 90) -> green
    std::uint32_t client_pixel = *reinterpret_cast<std::uint32_t*>(raw + (90 * disp->stride()) + (60 * 4));
    TEST_ASSERT(client_pixel == 0xFF00FF00);

    std::cout << "[PASS] test_compositor_background_and_window_blit passed\n";
}

void test_damage_tracking_and_frame_skipping() {
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 600;
    disp_cfg.height = 400;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    MultiWindowCompositor comp(wm, disp);

    auto s1 = std::make_shared<DisplaySurface>(1, 10, 200, 100);
    wm->create_window(10, "App 1", Rect{50, 50, 202, 129}, s1);

    // Frame 1: initial composition
    TEST_ASSERT(comp.composite());
    TEST_ASSERT(comp.stats().total_frames == 1);

    // Frame 2: no changes, nothing dirty -> must skip frame!
    TEST_ASSERT(comp.composite());
    TEST_ASSERT(comp.stats().skipped_frames == 1);
    TEST_ASSERT(comp.stats().total_frames == 1);

    // Frame 3: mark window dirty -> partial redraw
    wm->mark_window_dirty(1, Rect{50, 50, 100, 100});
    TEST_ASSERT(comp.composite());
    TEST_ASSERT(comp.stats().total_frames == 2);
    TEST_ASSERT(comp.stats().partial_redraw_frames == 1);

    std::cout << "[PASS] test_damage_tracking_and_frame_skipping passed\n";
}

void test_occlusion_optimization() {
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 600;
    disp_cfg.height = 400;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    MultiWindowCompositor comp(wm, disp);

    auto s_under = std::make_shared<DisplaySurface>(1, 10, 150, 100);
    auto s_over  = std::make_shared<DisplaySurface>(2, 20, 300, 250);

    // Lower window at (100, 100, 150, 100)
    std::uint32_t w_under = wm->create_window(10, "Under", Rect{100, 100, 150, 100}, s_under);
    // Higher window at (80, 80, 300, 250) completely covers (100, 100, 150, 100)
    std::uint32_t w_over  = wm->create_window(20, "Over", Rect{80, 80, 300, 250}, s_over);
    TEST_ASSERT(w_under > 0 && w_over > 0);

    TEST_ASSERT(comp.composite(true));
    TEST_ASSERT(comp.stats().occluded_windows_skipped == 1);

    std::cout << "[PASS] test_occlusion_optimization passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.11 Multi-Window Compositor & Damage Tests ===" << std::endl;
    test_compositor_background_and_window_blit();
    test_damage_tracking_and_frame_skipping();
    test_occlusion_optimization();
    std::cout << "=== All Stage 216.11 Multi-Window Compositor Tests PASSED ===" << std::endl;
    return 0;
}
