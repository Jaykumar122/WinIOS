#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"
#include "kernel/guest_trace_logger.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::input;
using namespace winios::graphics;

void test_mouse_routing_and_hit_testing() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    // Create two windows:
    // Window 1 (PID 100): at (10, 10, 100, 100)
    // Window 2 (PID 200): at (150, 10, 100, 100)
    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 100, 100);
    auto surf2 = std::make_shared<DisplaySurface>(2, 200, 100, 100);
    std::uint32_t w1 = win_mgr->create_window(100, "Window 1", Rect{10, 10, 100, 100}, surf1);
    std::uint32_t w2 = win_mgr->create_window(200, "Window 2", Rect{150, 10, 100, 100}, surf2);
    TEST_ASSERT(w1 > 0 && w2 > 0);

    // 1. Mouse move over Window 1
    InputEvent move_w1{};
    move_w1.type = InputEventType::MouseMove;
    move_w1.mouse.x = 50;
    move_w1.mouse.y = 50;
    TEST_ASSERT(router.route_event(move_w1));

    InputEvent popped{};
    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.target_pid == 100);
    TEST_ASSERT(popped.target_window_id == w1);
    TEST_ASSERT(!router.poll_event(200, popped)); // PID 200 should have no events

    // 2. Mouse move over Window 2
    InputEvent move_w2{};
    move_w2.type = InputEventType::MouseMove;
    move_w2.mouse.x = 180;
    move_w2.mouse.y = 50;
    TEST_ASSERT(router.route_event(move_w2));

    TEST_ASSERT(router.poll_event(200, popped));
    TEST_ASSERT(popped.target_pid == 200);
    TEST_ASSERT(popped.target_window_id == w2);

    // 3. Mouse Wheel over Window 1
    InputEvent wheel_ev{};
    wheel_ev.type = InputEventType::MouseWheel;
    wheel_ev.mouse.x = 50;
    wheel_ev.mouse.y = 50;
    wheel_ev.mouse.wheel_delta = 120;
    TEST_ASSERT(router.route_event(wheel_ev));
    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.type == InputEventType::MouseWheel);
    TEST_ASSERT(popped.mouse.wheel_delta == 120);

    std::cout << "[PASS] test_mouse_routing_and_hit_testing passed\n";
}

void test_mouse_capture_lifecycle() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 100, 100);
    auto surf2 = std::make_shared<DisplaySurface>(2, 200, 100, 100);
    std::uint32_t w1 = win_mgr->create_window(100, "Window 1", Rect{10, 10, 100, 100}, surf1);
    std::uint32_t w2 = win_mgr->create_window(200, "Window 2", Rect{150, 10, 100, 100}, surf2);
    TEST_ASSERT(w1 > 0 && w2 > 0);

    TEST_ASSERT(!router.is_mouse_captured());

    // 1. Mouse down inside Window 1 -> should acquire mouse capture to Window 1
    InputEvent down_ev{};
    down_ev.type = InputEventType::MouseDown;
    down_ev.mouse.button = MouseButton::Left;
    down_ev.mouse.x = 50;
    down_ev.mouse.y = 50;
    TEST_ASSERT(router.route_event(down_ev));

    TEST_ASSERT(router.is_mouse_captured());
    TEST_ASSERT(router.mouse_captured_window_id() == w1);
    TEST_ASSERT(router.mouse_captured_pid() == 100);

    InputEvent popped{};
    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.type == InputEventType::MouseDown);

    // 2. Drag mouse over Window 2's area -> due to capture, must route to Window 1 (PID 100)!
    InputEvent drag_ev{};
    drag_ev.type = InputEventType::MouseMove;
    drag_ev.mouse.x = 180; // inside Window 2
    drag_ev.mouse.y = 50;
    TEST_ASSERT(router.route_event(drag_ev));

    TEST_ASSERT(!router.poll_event(200, popped)); // Window 2 must NOT receive this event!
    TEST_ASSERT(router.poll_event(100, popped));  // Window 1 receives captured event
    TEST_ASSERT(popped.target_window_id == w1);
    TEST_ASSERT(popped.mouse.x == 180);

    // 3. Mouse up outside Window 1 (over Window 2) -> delivered to Window 1, then released
    InputEvent up_ev{};
    up_ev.type = InputEventType::MouseUp;
    up_ev.mouse.button = MouseButton::Left;
    up_ev.mouse.x = 180;
    up_ev.mouse.y = 50;
    TEST_ASSERT(router.route_event(up_ev));

    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.type == InputEventType::MouseUp);
    TEST_ASSERT(!router.is_mouse_captured()); // Capture must now be released!

    // 4. Next move over Window 2 routes to Window 2 now that capture is released
    InputEvent move2{};
    move2.type = InputEventType::MouseMove;
    move2.mouse.x = 180;
    move2.mouse.y = 50;
    TEST_ASSERT(router.route_event(move2));
    TEST_ASSERT(router.poll_event(200, popped));
    TEST_ASSERT(popped.target_window_id == w2);

    std::cout << "[PASS] test_mouse_capture_lifecycle passed\n";
}

void test_mouse_coordinate_translation() {
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 1179;
    disp_cfg.height = 2556;

    // Map (100, 200) in a 400x800 host window
    auto tp = InputRouter::translate_host_coordinate(100, 200, 400, 800, disp_cfg);
    TEST_ASSERT(tp.is_active);
    // scale_x = 1179 / 400 = 2.9475 -> 100 * 2.9475 = 294
    TEST_ASSERT(tp.x >= 290 && tp.x <= 300);
    // scale_y = 2556 / 800 = 3.195 -> 200 * 3.195 = 639
    TEST_ASSERT(tp.y >= 635 && tp.y <= 645);

    // Clamping checks
    auto tp_clamped = InputRouter::translate_host_coordinate(500, 1000, 400, 800, disp_cfg);
    TEST_ASSERT(tp_clamped.x == 1178); // max width - 1
    TEST_ASSERT(tp_clamped.y == 2555); // max height - 1

    std::cout << "[PASS] test_mouse_coordinate_translation passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.3 Mouse Input Tests ===" << std::endl;
    test_mouse_routing_and_hit_testing();
    test_mouse_capture_lifecycle();
    test_mouse_coordinate_translation();
    std::cout << "=== All Stage 216.3 Mouse Input Tests PASSED ===" << std::endl;
    return 0;
}
