#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/multi_window_compositor.h"
#include "graphics/desktop_controller.h"
#include "devices/input_device_service.h"
#include "devices/display_service.h"
#include "devices/notification_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;
using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 226.7] Keyboard Focus, Event Routing & Text Input...\n" << std::flush;

    DisplayConfiguration disp_cfg;
    disp_cfg.width = 1024;
    disp_cfg.height = 768;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);

    auto wm = std::make_shared<WindowManager>();
    auto comp = std::make_shared<MultiWindowCompositor>(wm, disp);
    auto ptr_dev = std::make_shared<VirtualPointerDevice>();
    auto kbd_dev = std::make_shared<VirtualKeyboardDevice>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    DesktopController controller(wm, comp, disp, nullptr, ptr_dev, kbd_dev, notif_svc);

    // 1. Create two windows
    auto s1 = std::make_shared<DisplaySurface>(1, 1001, 300, 200);
    uint64_t h1 = wm->create_window_v2(1001, "Editor 1", Rect{50, 50, 300, 200}, s1);
    auto win1 = wm->get_window_by_handle(h1);

    auto s2 = std::make_shared<DisplaySurface>(2, 1002, 300, 200);
    uint64_t h2 = wm->create_window_v2(1002, "Editor 2", Rect{400, 50, 300, 200}, s2);
    auto win2 = wm->get_window_by_handle(h2);

    // Initial focus: win2 is created last, so it has focus
    TEST_ASSERT(wm->focused_window_id() == window_handle_id(h2));

    // 2. Route KeyDown / KeyUp to focused window (win2)
    TEST_ASSERT(controller.handle_key_event(65 /* 'A' */, true, 0x01 /* Shift */, 1000));
    TEST_ASSERT(controller.handle_key_event(65 /* 'A' */, false, 0, 2000));

    // Win2 should receive both events
    DesktopEvent ev1{}, ev2{};
    TEST_ASSERT(win2->event_queue.pop(ev1));
    TEST_ASSERT(ev1.event_type == static_cast<uint32_t>(DesktopEventType::KeyDown));
    TEST_ASSERT(ev1.button_or_key == 65);
    TEST_ASSERT(ev1.modifiers == 0x01);

    TEST_ASSERT(win2->event_queue.pop(ev2));
    TEST_ASSERT(ev2.event_type == static_cast<uint32_t>(DesktopEventType::KeyUp));
    TEST_ASSERT(ev2.button_or_key == 65);

    // Win1 should have empty queue
    DesktopEvent dummy{};
    TEST_ASSERT(!win1->event_queue.pop(dummy));

    // 3. Text Input to focused window (win2)
    TEST_ASSERT(controller.handle_text_input("Hello WinIOS!", 3000));
    DesktopEvent text_ev{};
    TEST_ASSERT(win2->event_queue.pop(text_ev));
    TEST_ASSERT(text_ev.event_type == static_cast<uint32_t>(DesktopEventType::TextInput));
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(text_ev.payload)) == "Hello WinIOS!");

    // 4. Switch focus to win1 via click
    // Win1 client area is at (50+1, 50+28) = (51, 78)
    TEST_ASSERT(controller.handle_pointer_button(1, true, 100, 100, 4000));
    TEST_ASSERT(controller.handle_pointer_button(1, false, 100, 100, 4500));
    TEST_ASSERT(wm->focused_window_id() == window_handle_id(h1));

    // Drain click events from win1
    while (win1->event_queue.pop(dummy)) {}

    // Send text input now - should go to win1!
    TEST_ASSERT(controller.handle_text_input("Focused on 1", 5000));
    TEST_ASSERT(win1->event_queue.pop(text_ev));
    TEST_ASSERT(text_ev.event_type == static_cast<uint32_t>(DesktopEventType::TextInput));
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(text_ev.payload)) == "Focused on 1");

    // Win2 should have nothing new
    TEST_ASSERT(!win2->event_queue.pop(dummy));

    std::cout << "[Test Stage 226.7] PASSED successfully.\n" << std::flush;
    return 0;
}
