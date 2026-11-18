#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

void test_per_process_window_isolation() {
    std::cout << "[Test 1] Per-process window isolation...\n";
    WindowManager wm;

    auto surfA1 = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surfA2 = std::make_shared<DisplaySurface>(2, 10, 200, 200);
    auto surfB1 = std::make_shared<DisplaySurface>(3, 20, 300, 200);
    auto surfB2 = std::make_shared<DisplaySurface>(4, 20, 300, 200);

    uint32_t widA1 = wm.create_window(10, "AppA Win1", Rect{0, 0, 200, 200}, surfA1);
    uint32_t widA2 = wm.create_window(10, "AppA Win2", Rect{20, 20, 200, 200}, surfA2);
    uint32_t widB1 = wm.create_window(20, "AppB Win1", Rect{100, 100, 300, 200}, surfB1);
    uint32_t widB2 = wm.create_window(20, "AppB Win2", Rect{120, 120, 300, 200}, surfB2);

    TEST_ASSERT(widA1 > 0 && widA2 > 0 && widB1 > 0 && widB2 > 0);
    TEST_ASSERT(wm.window_count() == 4);

    // Enumeration test
    auto winsA = wm.get_windows_for_process(10);
    auto winsB = wm.get_windows_for_process(20);
    auto winsC = wm.get_windows_for_process(30);

    TEST_ASSERT(winsA.size() == 2);
    TEST_ASSERT(winsB.size() == 2);
    TEST_ASSERT(winsC.empty());

    // Isolation: Process 20 cannot destroy Process 10's window
    TEST_ASSERT(!wm.destroy_window(20, widA1));
    TEST_ASSERT(wm.get_window(widA1) != nullptr);

    // Isolation: Process 20 cannot move or resize Process 10's window
    TEST_ASSERT(!wm.move_window(20, widA1, 50, 50));
    TEST_ASSERT(!wm.resize_window(20, widA1, 400, 400));
    TEST_ASSERT(!wm.set_window_frame(20, widA1, Rect{50, 50, 400, 400}));
    TEST_ASSERT(!wm.set_window_state(20, widA1, WindowState::Minimized));

    // Owner (Process 10) can modify its own window
    TEST_ASSERT(wm.move_window(10, widA1, 10, 10));
    TEST_ASSERT(wm.get_window(widA1)->frame.x == 10);
    TEST_ASSERT(wm.destroy_window(10, widA1));
    TEST_ASSERT(wm.get_window(widA1) == nullptr);
    TEST_ASSERT(wm.window_count() == 3);

    std::cout << "[PASS] Per-process window isolation passed.\n";
}

void test_z_order_stacking_and_focus() {
    std::cout << "[Test 2] Z-order stacking and automatic focus reassignment...\n";
    WindowManager wm;

    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 200, 150);
    auto surf2 = std::make_shared<DisplaySurface>(2, 100, 200, 150);
    auto surf3 = std::make_shared<DisplaySurface>(3, 200, 200, 150);

    uint32_t wid1 = wm.create_window(100, "Window 1", Rect{0, 0, 200, 150}, surf1);
    uint32_t wid2 = wm.create_window(100, "Window 2", Rect{50, 50, 200, 150}, surf2);
    uint32_t wid3 = wm.create_window(200, "Window 3", Rect{100, 100, 200, 150}, surf3);

    // Initial focus on latest or raised
    wm.set_focus(wid2);
    TEST_ASSERT(wm.focused_window_id() == wid2);
    TEST_ASSERT(wm.focused_pid() == 100);

    // Raise window 1 to top
    TEST_ASSERT(wm.raise_to_top(wid1));
    auto z_wins = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(z_wins.size() == 3);
    TEST_ASSERT(z_wins.back()->id == wid1); // Topmost (painter's algorithm order)

    // Destroy focused window (wid2) and verify focus reassigns to topmost visible (wid1)
    TEST_ASSERT(wm.destroy_window(100, wid2));
    TEST_ASSERT(wm.focused_window_id() == wid1);
    TEST_ASSERT(wm.focused_pid() == 100);

    // Lower window 1 to bottom; bottommost should now be window 1, topmost window 3
    TEST_ASSERT(wm.lower_to_bottom(wid1));
    z_wins = wm.get_visible_windows_z_ordered();
    TEST_ASSERT(z_wins.front()->id == wid1);
    TEST_ASSERT(z_wins.back()->id == wid3);

    std::cout << "[PASS] Z-order stacking and focus reassignment passed.\n";
}

void test_process_termination_cleanup() {
    std::cout << "[Test 3] Process termination zero-leak window cleanup...\n";
    WindowManager wm;

    auto surfA1 = std::make_shared<DisplaySurface>(1, 50, 200, 150);
    auto surfA2 = std::make_shared<DisplaySurface>(2, 50, 200, 150);
    auto surfB1 = std::make_shared<DisplaySurface>(3, 60, 200, 150);

    uint32_t widA1 = wm.create_window(50, "AppA 1", Rect{0, 0, 200, 150}, surfA1);
    uint32_t widA2 = wm.create_window(50, "AppA 2", Rect{20, 20, 200, 150}, surfA2);
    uint32_t widB1 = wm.create_window(60, "AppB 1", Rect{50, 50, 200, 150}, surfB1);

    TEST_ASSERT(wm.window_count() == 3);
    wm.set_focus(widA1);
    TEST_ASSERT(wm.focused_pid() == 50);

    // Process 50 abruptly exits
    wm.destroy_windows_for_process(50);
    TEST_ASSERT(wm.window_count() == 1);
    TEST_ASSERT(wm.get_window(widA1) == nullptr);
    TEST_ASSERT(wm.get_window(widA2) == nullptr);
    TEST_ASSERT(wm.get_window(widB1) != nullptr);

    // Focus automatically transferred to survivor process
    TEST_ASSERT(wm.focused_window_id() == widB1);
    TEST_ASSERT(wm.focused_pid() == 60);

    std::cout << "[PASS] Process termination cleanup passed.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.4: WINDOW SERVER TESTS\n";
    std::cout << "========================================\n";

    test_per_process_window_isolation();
    test_z_order_stacking_and_focus();
    test_process_termination_cleanup();

    std::cout << "\nALL STAGE 222.4 TESTS PASSED (100%).\n";
    return 0;
}
