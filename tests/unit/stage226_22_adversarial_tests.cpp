#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/window_event_queue.h"
#include "graphics/desktop_controller.h"
#include "graphics/virtual_display.h"
#include "graphics/multi_window_compositor.h"
#include "devices/input_device_service.h"
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
    std::cout << "[Test Stage 226.22] Adversarial Window Management & Stress Tests...\n" << std::flush;

    WindowManager wm;

    // 1. Forged Handle Testing
    const uint64_t forged_handles[] = {
        0ULL,
        ~0ULL,
        0x0000000100000000ULL, // Generation 1, ID 0
        0x0000000000000001ULL, // Generation 0, ID 1
        0xDEADBEEF00000042ULL, // Forged generation
        0x00000001FFFFFFFFULL  // Out-of-bounds ID
    };

    for (uint64_t h : forged_handles) {
        TEST_ASSERT(!wm.is_handle_valid(h));
        TEST_ASSERT(wm.get_window_by_handle(h) == nullptr);
        TEST_ASSERT(!wm.destroy_window_by_handle(101, h));
        TEST_ASSERT(!wm.minimize_window(101, h));
        TEST_ASSERT(!wm.maximize_window(101, h, Rect{0, 0, 800, 600}));
        TEST_ASSERT(!wm.restore_window(101, h));
        DesktopEvent ev{};
        TEST_ASSERT(!wm.poll_event(101, h, ev));
        TEST_ASSERT(!wm.invalidate_window_rect(101, h, Rect{0, 0, 10, 10}));
    }

    // 2. Cross-Process Tampering Denial
    uint64_t h_alice = wm.create_window_v2(1001, "Alice Window", Rect{50, 50, 200, 200}, nullptr);
    TEST_ASSERT(h_alice != 0);

    // Eve (PID 1337) attempts to manipulate Alice's window
    TEST_ASSERT(!wm.destroy_window_by_handle(1337, h_alice));
    TEST_ASSERT(!wm.minimize_window(1337, h_alice));
    TEST_ASSERT(!wm.maximize_window(1337, h_alice, Rect{0, 0, 800, 600}));
    TEST_ASSERT(!wm.restore_window(1337, h_alice));
    DesktopEvent eve_polled{};
    TEST_ASSERT(!wm.poll_event(1337, h_alice, eve_polled));
    TEST_ASSERT(!wm.invalidate_window_rect(1337, h_alice, Rect{0, 0, 50, 50}));

    // Alice can still operate normally
    TEST_ASSERT(wm.is_handle_valid(h_alice));
    TEST_ASSERT(wm.minimize_window(1001, h_alice));
    TEST_ASSERT(wm.restore_window(1001, h_alice));

    // 3. Event Queue Flooding / DoS Protection
    WindowEventQueue queue;
    for (int i = 0; i < 50000; ++i) {
        DesktopEvent ev{};
        ev.event_type = static_cast<uint32_t>(DesktopEventType::WindowStateChanged);
        ev.button_or_key = i;
        queue.push(ev);
    }
    // Must remain capped at bounded size 128
    TEST_ASSERT(queue.size() <= 128);
    TEST_ASSERT(queue.dropped_count() >= 49800);

    // Drain queue to verify queue remains intact and uncorrupted
    size_t drained = 0;
    DesktopEvent drain_ev{};
    while (queue.pop(drain_ev)) {
        ++drained;
    }
    TEST_ASSERT(drained <= 128);
    TEST_ASSERT(queue.empty());

    // 4. Window Creation / Destruction Churn (2000 cycles)
    for (int i = 0; i < 2000; ++i) {
        uint32_t pid = 2000 + (i % 8);
        uint64_t handle = wm.create_window_v2(pid, "Churn Win", Rect{10, 10, 100, 100}, nullptr);
        TEST_ASSERT(handle != 0);
        TEST_ASSERT(wm.is_handle_valid(handle));

        // Rapid invalidate and event post
        DesktopEvent ev{};
        ev.event_type = static_cast<uint32_t>(DesktopEventType::KeyDown);
        ev.button_or_key = i % 256;
        wm.post_event(handle, ev);

        // Immediate destroy
        TEST_ASSERT(wm.destroy_window_by_handle(pid, handle));
        TEST_ASSERT(!wm.is_handle_valid(handle));
    }

    // 5. Extreme Non-Client Hit Testing Bounds
    DisplayConfiguration disp_cfg;
    disp_cfg.width = 800;
    disp_cfg.height = 600;
    auto disp = std::make_shared<VirtualDisplay>(disp_cfg);
    auto comp = std::make_shared<MultiWindowCompositor>(std::make_shared<WindowManager>(), disp);
    auto ptr_dev = std::make_shared<VirtualPointerDevice>();
    auto kbd_dev = std::make_shared<VirtualKeyboardDevice>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    DesktopController controller(std::make_shared<WindowManager>(), comp, disp, nullptr, ptr_dev, kbd_dev, notif_svc);

    const int32_t extreme_coords[][2] = {
        {-100000, -100000},
        {100000, 100000},
        {-1, -1},
        {0x7FFFFFFF, 0x7FFFFFFF},
        {-0x7FFFFFFF, -0x7FFFFFFF}
    };

    for (const auto& pt : extreme_coords) {
        TEST_ASSERT(controller.handle_pointer_move(pt[0], pt[1], 1000));
        TEST_ASSERT(controller.handle_pointer_button(1, true, pt[0], pt[1], 1000));
        TEST_ASSERT(controller.handle_pointer_button(1, false, pt[0], pt[1], 1000));
    }

    std::cout << "[Test Stage 226.22] PASSED successfully.\n" << std::flush;
    return 0;
}
