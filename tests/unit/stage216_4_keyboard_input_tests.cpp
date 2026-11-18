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

void test_keyboard_event_routing_to_focused_window() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 100, 100);
    auto surf2 = std::make_shared<DisplaySurface>(2, 200, 100, 100);
    std::uint32_t w1 = win_mgr->create_window(100, "Window 1", Rect{10, 10, 100, 100}, surf1);
    std::uint32_t w2 = win_mgr->create_window(200, "Window 2", Rect{150, 10, 100, 100}, surf2);
    TEST_ASSERT(w1 > 0 && w2 > 0);

    // Focus Window 1
    win_mgr->set_focus(w1);
    TEST_ASSERT(win_mgr->focused_window_id() == w1);

    // 1. KeyPress routed to Window 1
    InputEvent key_down{};
    key_down.type = InputEventType::KeyPress;
    key_down.keyboard.virtual_key = 0x41; // 'A'
    key_down.keyboard.is_down = true;
    key_down.keyboard.modifiers = 1; // Shift
    TEST_ASSERT(router.route_event(key_down));

    InputEvent popped{};
    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.target_pid == 100);
    TEST_ASSERT(popped.target_window_id == w1);
    TEST_ASSERT(popped.keyboard.virtual_key == 0x41);
    TEST_ASSERT(popped.keyboard.modifiers == 1);
    TEST_ASSERT(!router.poll_event(200, popped));

    // 2. CharInput routed to Window 1
    InputEvent char_ev{};
    char_ev.type = InputEventType::CharInput;
    char_ev.keyboard.char_code = U'A';
    TEST_ASSERT(router.route_event(char_ev));

    TEST_ASSERT(router.poll_event(100, popped));
    TEST_ASSERT(popped.type == InputEventType::CharInput);
    TEST_ASSERT(popped.keyboard.char_code == U'A');

    // 3. Switch focus to Window 2
    win_mgr->set_focus(w2);
    TEST_ASSERT(win_mgr->focused_window_id() == w2);

    // Next key event routes to Window 2 (PID 200)
    InputEvent key_down2{};
    key_down2.type = InputEventType::KeyPress;
    key_down2.keyboard.virtual_key = 0x42; // 'B'
    TEST_ASSERT(router.route_event(key_down2));

    TEST_ASSERT(!router.poll_event(100, popped));
    TEST_ASSERT(router.poll_event(200, popped));
    TEST_ASSERT(popped.target_pid == 200);
    TEST_ASSERT(popped.target_window_id == w2);

    std::cout << "[PASS] test_keyboard_event_routing_to_focused_window passed\n";
}

void test_keyboard_routing_default_shell() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    // No windows created -> should route to PID 1 (shell)
    InputEvent key_ev{};
    key_ev.type = InputEventType::KeyPress;
    key_ev.keyboard.virtual_key = 0x0D; // Enter
    TEST_ASSERT(router.route_event(key_ev));

    InputEvent popped{};
    TEST_ASSERT(router.poll_event(1, popped));
    TEST_ASSERT(popped.target_pid == 1);
    TEST_ASSERT(popped.keyboard.virtual_key == 0x0D);

    std::cout << "[PASS] test_keyboard_routing_default_shell passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.4 Keyboard Input Tests ===" << std::endl;
    test_keyboard_event_routing_to_focused_window();
    test_keyboard_routing_default_shell();
    std::cout << "=== All Stage 216.4 Keyboard Input Tests PASSED ===" << std::endl;
    return 0;
}
