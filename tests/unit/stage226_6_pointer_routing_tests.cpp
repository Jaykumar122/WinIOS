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
    std::cout << "[Test Stage 226.6] Pointer Routing, Hit-Testing & Titlebar Dragging...\n" << std::flush;

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

    // 1. Create a window at (100, 100, 300, 200)
    auto s1 = std::make_shared<DisplaySurface>(1, 1001, 300, 200);
    uint64_t h1 = wm->create_window_v2(1001, "Test Window 1", Rect{100, 100, 300, 200}, s1);
    TEST_ASSERT(h1 != 0);

    auto win = wm->get_window_by_handle(h1);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->frame.x == 100);
    TEST_ASSERT(win->frame.y == 100);

    // 2. Client Click Test
    // Client area starts at y = 100 + 28 = 128.
    // Click at (150, 150): should be in client frame.
    TEST_ASSERT(controller.handle_pointer_button(1, true, 150, 150, 1000));
    TEST_ASSERT(controller.handle_pointer_button(1, false, 150, 150, 2000));

    // Check window event queue
    DesktopEvent ev1{}, ev2{};
    TEST_ASSERT(win->event_queue.pop(ev1));
    TEST_ASSERT(ev1.event_type == static_cast<uint32_t>(DesktopEventType::PointerDown));
    TEST_ASSERT(ev1.button_or_key == 1);
    TEST_ASSERT(ev1.screen_x == 150);
    TEST_ASSERT(ev1.screen_y == 150);

    TEST_ASSERT(win->event_queue.pop(ev2));
    TEST_ASSERT(ev2.event_type == static_cast<uint32_t>(DesktopEventType::PointerUp));
    TEST_ASSERT(ev2.button_or_key == 1);

    // 3. Titlebar Dragging Test
    // Titlebar is at y in [100, 128), let's click at (200, 110)
    // Non-client buttons are at right:
    // Window width = 300, close button is at [100 + 300 - 20 - 8, ...]
    // So (200, 110) is clearly in the draggable titlebar area.
    TEST_ASSERT(controller.handle_pointer_button(1, true, 200, 110, 3000));
    TEST_ASSERT(controller.is_dragging());

    // Drag pointer to (250, 160) - delta is (+50, +50)
    TEST_ASSERT(controller.handle_pointer_move(250, 160, 4000));
    TEST_ASSERT(win->frame.x == 150);
    TEST_ASSERT(win->frame.y == 150);

    // Release mouse button
    TEST_ASSERT(controller.handle_pointer_button(1, false, 250, 160, 5000));
    TEST_ASSERT(!controller.is_dragging());
    TEST_ASSERT(win->frame.x == 150);
    TEST_ASSERT(win->frame.y == 150);

    // 4. Non-client Minimize Button Test
    // Window is at (150, 150, 300, 200).
    // Minimize button is around:
    // close: right - 8 - 18 = 150 + 300 - 26 = 424
    // max: close - 24 = 400
    // min: max - 24 = 376
    // y in [150 + 6, 150 + 22] = [156, 172]
    // Let's click at (380, 160):
    TEST_ASSERT(controller.handle_pointer_button(1, true, 380, 160, 6000));
    TEST_ASSERT(win->state == WindowState::Minimized);

    // 5. Restore and Maximize Button Test
    wm->restore_window(1001, h1);
    TEST_ASSERT(win->state == WindowState::Normal);

    // Click Maximize button at (404, 160):
    TEST_ASSERT(controller.handle_pointer_button(1, true, 404, 160, 7000));
    TEST_ASSERT(win->state == WindowState::Maximized);

    // Click again to toggle/restore:
    // Window frame is now maximized (0, 0, 1024, 768)
    // Max button at 1024 - 8 - 18 - 24 = 974
    TEST_ASSERT(controller.handle_pointer_button(1, true, 978, 14, 8000));
    TEST_ASSERT(win->state == WindowState::Normal);

    // 6. Close Button Test
    // Window is back at (150, 150, 300, 200).
    // Close button is at (430, 160):
    TEST_ASSERT(controller.handle_pointer_button(1, true, 430, 160, 9000));

    DesktopEvent close_ev{};
    bool found_close = false;
    while (win->event_queue.pop(close_ev)) {
        if (close_ev.event_type == static_cast<uint32_t>(DesktopEventType::WindowCloseRequest)) {
            found_close = true;
            break;
        }
    }
    TEST_ASSERT(found_close);

    std::cout << "[Test Stage 226.6] PASSED successfully.\n" << std::flush;
    return 0;
}
