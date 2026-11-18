#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"
#include "input/input_provenance.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;
using namespace winios::input;

void test_coordinate_translation() {
    std::cout << "[Test 1] Host coordinate translation and aspect scaling...\n";
    DisplayConfiguration cfg;
    cfg.width = 1179;
    cfg.height = 2556;

    // Center of 800x600 host window -> Center of 1179x2556 virtual display
    TouchPoint tp = InputRouter::translate_host_coordinate(400, 300, 800, 600, cfg);
    TEST_ASSERT(tp.is_active);
    TEST_ASSERT(tp.x >= 588 && tp.x <= 590);
    TEST_ASSERT(tp.y >= 1277 && tp.y <= 1279);

    // Clamping on negative coordinates
    TouchPoint tp_neg = InputRouter::translate_host_coordinate(-10, -20, 800, 600, cfg);
    TEST_ASSERT(tp_neg.x == 0 && tp_neg.y == 0);

    // Clamping on overflow
    TouchPoint tp_over = InputRouter::translate_host_coordinate(900, 700, 800, 600, cfg);
    TEST_ASSERT(tp_over.x == 1178);
    TEST_ASSERT(tp_over.y == 2555);

    std::cout << "[PASS] Coordinate translation verified.\n";
}

void test_hit_testing_and_focus_switching() {
    std::cout << "[Test 2] Hit-testing and automatic focus switching on mouse click...\n";
    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surfA = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surfB = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    uint32_t widA = wm->create_window(10, "AppA", Rect{0, 0, 200, 200}, surfA);
    uint32_t widB = wm->create_window(20, "AppB", Rect{100, 100, 200, 200}, surfB);

    wm->set_focus(widA);
    TEST_ASSERT(wm->focused_window_id() == widA);

    // Click inside Window B (overlapping region at 150, 150)
    InputEvent click{};
    click.type = InputEventType::MouseDown;
    click.mouse.x = 150;
    click.mouse.y = 150;
    click.mouse.button = MouseButton::Left;
    click.provenance = InputProvenance::PhysicalWindowsInput;
    click.win32_msg_id = 101;

    TEST_ASSERT(router.route_event(click));

    // Focus must switch to Window B (topmost at 150, 150)
    TEST_ASSERT(wm->focused_window_id() == widB);
    TEST_ASSERT(wm->focused_pid() == 20);

    // Process 20 receives the event
    InputEvent polled{};
    TEST_ASSERT(router.poll_event(20, polled));
    TEST_ASSERT(polled.type == InputEventType::MouseDown);
    TEST_ASSERT(polled.target_window_id == widB);
    TEST_ASSERT(polled.target_pid == 20);

    std::cout << "[PASS] Hit-testing and focus switching verified.\n";
}

void test_mouse_drag_capture() {
    std::cout << "[Test 3] Mouse drag capture across window boundaries...\n";
    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surfA = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surfB = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    uint32_t widA = wm->create_window(10, "AppA", Rect{0, 0, 200, 200}, surfA);
    (void)widA;
    uint32_t widB = wm->create_window(20, "AppB", Rect{250, 250, 200, 200}, surfB);

    // 1. Mouse down inside Window B initiates capture
    InputEvent down{};
    down.type = InputEventType::MouseDown;
    down.mouse.x = 300;
    down.mouse.y = 300;
    down.mouse.button = MouseButton::Left;
    TEST_ASSERT(router.route_event(down));
    TEST_ASSERT(router.is_mouse_captured());
    TEST_ASSERT(router.mouse_captured_window_id() == widB);
    TEST_ASSERT(router.mouse_captured_pid() == 20);

    // 2. Drag cursor all the way into Window A at (50, 50)
    // Because Window B has capture, the event must still route to Window B!
    InputEvent move{};
    move.type = InputEventType::MouseMove;
    move.mouse.x = 50;
    move.mouse.y = 50;
    move.mouse.button = MouseButton::Left;
    TEST_ASSERT(router.route_event(move));

    InputEvent polled{};
    TEST_ASSERT(router.poll_event(20, polled)); // Down
    TEST_ASSERT(router.poll_event(20, polled)); // Move
    TEST_ASSERT(polled.target_window_id == widB);
    TEST_ASSERT(polled.target_pid == 20);

    // Window A received NO events
    TEST_ASSERT(!router.poll_event(10, polled));

    // 3. Mouse up releases capture
    InputEvent up{};
    up.type = InputEventType::MouseUp;
    up.mouse.x = 50;
    up.mouse.y = 50;
    up.mouse.button = MouseButton::Left;
    TEST_ASSERT(router.route_event(up));
    TEST_ASSERT(!router.is_mouse_captured());

    // 4. Subsequent move without capture at (50, 50) now routes to Window A
    InputEvent move2{};
    move2.type = InputEventType::MouseMove;
    move2.mouse.x = 50;
    move2.mouse.y = 50;
    TEST_ASSERT(router.route_event(move2));

    TEST_ASSERT(router.poll_event(10, polled));
    TEST_ASSERT(polled.target_window_id == widA);
    TEST_ASSERT(polled.target_pid == 10);

    std::cout << "[PASS] Mouse drag capture verified.\n";
}

void test_keyboard_focus_routing_and_provenance() {
    std::cout << "[Test 4] Keyboard routing to focused window and provenance tracking...\n";
    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surfA = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surfB = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    uint32_t widA = wm->create_window(10, "AppA", Rect{0, 0, 200, 200}, surfA);
    (void)widA;
    uint32_t widB = wm->create_window(20, "AppB", Rect{250, 250, 200, 200}, surfB);

    wm->set_focus(widB);

    // Send key press (no target window specified in raw message)
    InputEvent key_evt{};
    key_evt.type = InputEventType::KeyPress;
    key_evt.keyboard.virtual_key = 0x41; // 'A'
    key_evt.keyboard.is_down = true;
    key_evt.device_id = 99;
    key_evt.win32_msg_id = 0x0100; // WM_KEYDOWN
    key_evt.provenance = InputProvenance::PhysicalWindowsInput;
    key_evt.timestamp_us = 123456789ULL;

    TEST_ASSERT(router.route_event(key_evt));

    // Verified: routes to focused PID (20) and window (widB)
    InputEvent polled{};
    TEST_ASSERT(router.poll_event(20, polled));
    TEST_ASSERT(polled.type == InputEventType::KeyPress);
    TEST_ASSERT(polled.target_window_id == widB);
    TEST_ASSERT(polled.target_pid == 20);
    TEST_ASSERT(polled.device_id == 99);
    TEST_ASSERT(polled.provenance == InputProvenance::PhysicalWindowsInput);
    TEST_ASSERT(polled.win32_msg_id == 0x0100);

    // PID 10 received nothing
    TEST_ASSERT(!router.poll_event(10, polled));

    std::cout << "[PASS] Keyboard focus routing and provenance verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.8: INPUT PROVENANCE & ROUTING TESTS\n";
    std::cout << "========================================\n";

    test_coordinate_translation();
    test_hit_testing_and_focus_switching();
    test_mouse_drag_capture();
    test_keyboard_focus_routing_and_provenance();

    std::cout << "\nALL STAGE 222.8 TESTS PASSED (100%).\n";
    return 0;
}
