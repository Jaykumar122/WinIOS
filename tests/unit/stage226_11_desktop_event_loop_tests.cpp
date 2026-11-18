#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_event_queue.h"
#include "graphics/window_manager.h"
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
    std::cout << "[Test Stage 226.11] Desktop Event Queue & Bounded Loop Tests...\n" << std::flush;

    // 1. Test WindowEventQueue isolated behavior
    WindowEventQueue queue;
    TEST_ASSERT(queue.empty());
    TEST_ASSERT(queue.size() == 0);

    // Push 10 KeyDown events
    for (uint32_t i = 0; i < 10; ++i) {
        DesktopEvent ev{};
        ev.event_type = static_cast<uint32_t>(DesktopEventType::KeyDown);
        ev.button_or_key = i;
        TEST_ASSERT(queue.push(ev));
    }
    TEST_ASSERT(queue.size() == 10);

    // 2. Test PointerMove Coalescing
    // Pushing 5 consecutive PointerMove events should coalesce into 1
    for (uint32_t i = 0; i < 5; ++i) {
        DesktopEvent ev{};
        ev.event_type = static_cast<uint32_t>(DesktopEventType::PointerMove);
        ev.screen_x = 100 + i;
        ev.screen_y = 200 + i;
        queue.push(ev);
    }
    // Size should have increased by only 1 (from 10 to 11)
    TEST_ASSERT(queue.size() == 11);

    // 3. Test Queue Overflow Drops at 128 items
    // Push 200 different non-coalescing events
    for (uint32_t i = 0; i < 200; ++i) {
        DesktopEvent ev{};
        ev.event_type = static_cast<uint32_t>(DesktopEventType::WindowStateChanged);
        ev.button_or_key = i;
        queue.push(ev);
    }
    // Queue size must not exceed capacity (128)
    TEST_ASSERT(queue.size() <= 128);
    TEST_ASSERT(queue.dropped_count() > 0);

    // 4. Test Controller Integration and Stats
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 800;
    disp_cfg.height = 600;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);
    auto wm = std::make_shared<WindowManager>();
    auto comp = std::make_shared<MultiWindowCompositor>(wm, disp);
    auto ptr_dev = std::make_shared<VirtualPointerDevice>();
    auto kbd_dev = std::make_shared<VirtualKeyboardDevice>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    DesktopController controller(wm, comp, disp, nullptr, ptr_dev, kbd_dev, notif_svc);

    // Create a window
    uint64_t h = wm->create_window_v2(101, "Loop App", Rect{50, 50, 200, 200}, nullptr);
    TEST_ASSERT(h != 0);

    // Generate 50 pointer moves and 20 key presses
    for (int i = 0; i < 50; ++i) {
        controller.handle_pointer_move(100 + (i % 20), 100 + (i % 20), 1000 + i);
    }
    for (uint32_t k = 0; k < 20; ++k) {
        controller.handle_key_event(65 + k, true, 0, 2000 + k);
    }

    const auto& stats = controller.stats();
    TEST_ASSERT(stats.total_pointer_events == 50);
    TEST_ASSERT(stats.total_keyboard_events == 20);

    // Step controller
    TEST_ASSERT(controller.step());
    TEST_ASSERT(controller.stats().total_composited_frames >= 1);

    std::cout << "[Test Stage 226.11] PASSED successfully.\n" << std::flush;
    return 0;
}
