#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "graphics/window_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios::graphics;

void test_stacking_order_and_raise_lower() {
    WindowManager wm;
    auto s1 = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto s2 = std::make_shared<DisplaySurface>(2, 10, 200, 200);
    auto s3 = std::make_shared<DisplaySurface>(3, 10, 200, 200);

    std::uint32_t w1 = wm.create_window(10, "Window 1", Rect{0, 0, 200, 200}, s1);
    std::uint32_t w2 = wm.create_window(10, "Window 2", Rect{20, 20, 200, 200}, s2);
    std::uint32_t w3 = wm.create_window(10, "Window 3", Rect{40, 40, 200, 200}, s3);

    // Initial order should be w1 < w2 < w3
    auto order1 = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(order1.size() == 3);
    TEST_ASSERT(order1[0]->id == w1);
    TEST_ASSERT(order1[1]->id == w2);
    TEST_ASSERT(order1[2]->id == w3);

    // Raise w1 to top
    TEST_ASSERT(wm.raise_to_top(w1));
    TEST_ASSERT(wm.focused_window_id() == w1);
    auto order2 = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(order2[2]->id == w1);

    // Lower w1 to bottom
    TEST_ASSERT(wm.lower_to_bottom(w1));
    auto order3 = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(order3[0]->id == w1);

    std::cout << "[PASS] test_stacking_order_and_raise_lower passed\n";
}

void test_always_on_top_stacking() {
    WindowManager wm;
    auto s1 = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto s2 = std::make_shared<DisplaySurface>(2, 10, 200, 200);

    std::uint32_t w1 = wm.create_window(10, "Normal Win", Rect{0, 0, 200, 200}, s1);
    std::uint32_t w2 = wm.create_window(10, "AOT Win", Rect{20, 20, 200, 200}, s2);

    TEST_ASSERT(wm.set_always_on_top(10, w2, true));
    TEST_ASSERT(wm.get_window(w2)->always_on_top);

    // Now raise w1 (normal window) -> even with high z_order, always_on_top window must stay above it!
    TEST_ASSERT(wm.raise_to_top(w1));
    auto order = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(order.back()->id == w2); // w2 is always on top

    std::cout << "[PASS] test_always_on_top_stacking passed\n";
}

void test_window_movement_and_resizing_constraints() {
    WindowManager wm;
    auto s = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    std::uint32_t w = wm.create_window(10, "Test Win", Rect{50, 50, 200, 200}, s);

    // Move by (+30, -20)
    TEST_ASSERT(wm.move_window(10, w, 30, -20));
    auto win = wm.get_window(w);
    TEST_ASSERT(win->frame.x == 80);
    TEST_ASSERT(win->frame.y == 30);

    // Resize below minimum width (100) -> should clamp to min_width
    TEST_ASSERT(wm.resize_window(10, w, 50, 300));
    TEST_ASSERT(win->frame.width == win->min_width);
    TEST_ASSERT(win->frame.height == 300);

    std::cout << "[PASS] test_window_movement_and_resizing_constraints passed\n";
}

void test_multi_window_per_process() {
    WindowManager wm;
    auto s1 = std::make_shared<DisplaySurface>(1, 42, 300, 200);
    auto s2 = std::make_shared<DisplaySurface>(2, 42, 150, 150);

    std::uint32_t w1 = wm.create_window(42, "Editor Main", Rect{10, 10, 300, 200}, s1);
    std::uint32_t w2 = wm.create_window(42, "Tool Palette", Rect{320, 10, 150, 150}, s2);
    TEST_ASSERT(w1 != w2);

    auto p42_windows = wm.get_windows_for_process(42);
    TEST_ASSERT(p42_windows.size() == 2);

    std::cout << "[PASS] test_multi_window_per_process passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.7 Multi-Window Tests ===" << std::endl;
    test_stacking_order_and_raise_lower();
    test_always_on_top_stacking();
    test_window_movement_and_resizing_constraints();
    test_multi_window_per_process();
    std::cout << "=== All Stage 216.7 Multi-Window Tests PASSED ===" << std::endl;
    return 0;
}
