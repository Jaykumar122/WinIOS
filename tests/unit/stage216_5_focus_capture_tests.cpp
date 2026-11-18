#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::input;
using namespace winios::graphics;

void test_focus_and_window_destruction_cleanup() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 100, 100);
    auto surf2 = std::make_shared<DisplaySurface>(2, 200, 100, 100);
    std::uint32_t w1 = win_mgr->create_window(100, "Window 1", Rect{10, 10, 100, 100}, surf1);
    std::uint32_t w2 = win_mgr->create_window(200, "Window 2", Rect{150, 10, 100, 100}, surf2);
    TEST_ASSERT(w1 > 0 && w2 > 0);

    // Mouse down on Window 1 -> acquires capture
    InputEvent down_ev{};
    down_ev.type = InputEventType::MouseDown;
    down_ev.mouse.button = MouseButton::Left;
    down_ev.mouse.x = 50;
    down_ev.mouse.y = 50;
    TEST_ASSERT(router.route_event(down_ev));
    TEST_ASSERT(router.is_mouse_captured());
    TEST_ASSERT(router.mouse_captured_window_id() == w1);

    // Now destroy Window 1 while capture is active
    router.on_window_destroyed(w1);
    win_mgr->destroy_window(100, w1);

    // Mouse capture must be cleanly released!
    TEST_ASSERT(!router.is_mouse_captured());

    // Subsequent events to remaining window area route to Window 2
    InputEvent next_ev{};
    next_ev.type = InputEventType::MouseMove;
    next_ev.mouse.x = 180;
    next_ev.mouse.y = 50;
    TEST_ASSERT(router.route_event(next_ev));

    InputEvent popped{};
    TEST_ASSERT(router.poll_event(200, popped));
    TEST_ASSERT(popped.target_window_id == w2);

    std::cout << "[PASS] test_focus_and_window_destruction_cleanup passed\n";
}

void test_process_termination_cleanup() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf3 = std::make_shared<DisplaySurface>(3, 300, 100, 100);
    std::uint32_t w1 = win_mgr->create_window(300, "Window 300", Rect{10, 10, 100, 100}, surf3);
    TEST_ASSERT(w1 > 0);

    // Push 2 events for PID 300
    InputEvent ev1{};
    ev1.type = InputEventType::MouseMove;
    ev1.mouse.x = 20;
    ev1.mouse.y = 20;
    router.route_event(ev1);

    InputEvent ev2{};
    ev2.type = InputEventType::KeyPress;
    ev2.keyboard.virtual_key = 65;
    router.route_event(ev2);

    TEST_ASSERT(router.pending_events_for_process(300) > 0);

    // Terminate process 300
    router.on_process_terminated(300);
    win_mgr->destroy_window(300, w1);

    // Process queues and captures for PID 300 must be wiped
    TEST_ASSERT(router.pending_events_for_process(300) == 0);
    InputEvent popped{};
    TEST_ASSERT(!router.poll_event(300, popped));

    std::cout << "[PASS] test_process_termination_cleanup passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.5 Focus & Capture Tests ===" << std::endl;
    test_focus_and_window_destruction_cleanup();
    test_process_termination_cleanup();
    std::cout << "=== All Stage 216.5 Focus & Capture Tests PASSED ===" << std::endl;
    return 0;
}
