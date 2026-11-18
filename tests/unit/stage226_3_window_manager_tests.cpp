#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::graphics;

int main() {
    std::cout << "[Test Stage 226.3] Window Manager Core & Generation Handles...\n" << std::flush;

    WindowManager wm;

    // 1. Window Creation v2 with 64-bit generation handle
    Rect r1{100, 100, 400, 300};
    uint64_t h1 = wm.create_window_v2(1001, "Test Window 1", r1, nullptr);
    TEST_ASSERT(h1 != 0);
    TEST_ASSERT(window_handle_id(h1) == 1);
    TEST_ASSERT(window_handle_gen(h1) == 1);
    TEST_ASSERT(wm.is_handle_valid(h1));

    auto win1 = wm.get_window_by_handle(h1);
    TEST_ASSERT(win1 != nullptr);
    TEST_ASSERT(win1->owner_pid == 1001);
    TEST_ASSERT(win1->title == "Test Window 1");
    TEST_ASSERT(win1->state == WindowState::Normal);

    // 2. Window 2 creation
    Rect r2{150, 150, 300, 200};
    uint64_t h2 = wm.create_window_v2(1002, "Test Window 2", r2, nullptr);
    TEST_ASSERT(h2 != 0);
    TEST_ASSERT(window_handle_id(h2) == 2);
    TEST_ASSERT(window_handle_gen(h2) == 1);
    TEST_ASSERT(wm.is_handle_valid(h2));

    // 3. PID ownership enforcement
    // Process 1001 cannot destroy Process 1002's window
    TEST_ASSERT(!wm.destroy_window_by_handle(1001, h2));
    TEST_ASSERT(wm.is_handle_valid(h2));

    // Process 1002 can destroy its own window
    TEST_ASSERT(wm.destroy_window_by_handle(1002, h2));
    TEST_ASSERT(!wm.is_handle_valid(h2));

    // Stale handle cannot be retrieved
    TEST_ASSERT(wm.get_window_by_handle(h2) == nullptr);

    // 4. Stale handle rejection and new window allocation
    uint64_t h3 = wm.create_window_v2(1003, "New Window After Destroy", r2, nullptr);
    TEST_ASSERT(h3 != 0);
    TEST_ASSERT(window_handle_id(h3) == 3);
    TEST_ASSERT(wm.is_handle_valid(h3));
    TEST_ASSERT(!wm.is_handle_valid(h2)); // Old handle generation 1 is definitely stale!

    // 5. Window State Transitions: Minimize, Maximize, Restore
    TEST_ASSERT(wm.minimize_window(1001, h1));
    auto win1_check = wm.get_window_by_handle(h1);
    TEST_ASSERT(win1_check->state == WindowState::Minimized);

    TEST_ASSERT(wm.maximize_window(1001, h1, Rect{0, 0, 1920, 1080}));
    TEST_ASSERT(win1_check->state == WindowState::Maximized);

    TEST_ASSERT(wm.restore_window(1001, h1));
    TEST_ASSERT(win1_check->state == WindowState::Normal);

    // PID mismatch on state change should fail
    TEST_ASSERT(!wm.minimize_window(9999, h1));

    // 6. Invalidate window rect & damage tracking
    Rect dmg_sub{10, 10, 50, 50};
    TEST_ASSERT(wm.invalidate_window_rect(1001, h1, dmg_sub));
    TEST_ASSERT(win1_check->is_dirty);
    TEST_ASSERT(!wm.invalidate_window_rect(9999, h1, dmg_sub)); // PID mismatch

    // 7. Event Queue Operations
    DesktopEvent ev{};
    ev.event_type = static_cast<uint32_t>(DesktopEventType::WindowStateChanged);
    ev.button_or_key = 42;
    TEST_ASSERT(wm.post_event(h1, ev));

    DesktopEvent polled{};
    TEST_ASSERT(wm.poll_event(1001, h1, polled));
    TEST_ASSERT(polled.button_or_key == 42);
    TEST_ASSERT(!wm.poll_event(1001, h1, polled)); // Queue should now be empty

    // Poll with wrong PID should fail
    TEST_ASSERT(!wm.poll_event(9999, h1, polled));

    std::cout << "[Test Stage 226.3] PASSED successfully.\n" << std::flush;
    return 0;
}
