#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"

using namespace winios;
using namespace winios::graphics;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_window_decorations_and_hit_testing() {
    std::cout << "[Test 1] Window decorations and non-client zone hit testing...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 200, 200);

    // Create window at (50, 50, 300, 200)
    uint32_t wid = wm->create_window(100, "InteractionTest", Rect{50, 50, 300, 200}, surf);
    TEST_ASSERT(wid != 0);

    // Hit test title bar area (e.g. at 50 + 100 = 150, 50 + 10 = 60)
    NonClientZone zone = wm->hit_test_non_client(wid, 150, 60);
    TEST_ASSERT(zone == NonClientZone::TitleBar);

    // Hit test close button (top right: near 50 + 300 - 15 = 335, 50 + 10 = 60)
    NonClientZone close_zone = wm->hit_test_non_client(wid, 335, 60);
    TEST_ASSERT(close_zone == NonClientZone::CloseButton);

    // Hit test client area (inside client bounds: 50 + 20 = 70, 50 + 40 = 90)
    NonClientZone client_zone = wm->hit_test_non_client(wid, 70, 90);
    TEST_ASSERT(client_zone == NonClientZone::Client);

    // Hit test outside window bounds
    NonClientZone outside_zone = wm->hit_test_non_client(wid, 10, 10);
    TEST_ASSERT(outside_zone == NonClientZone::None);

    std::cout << "[PASS] Non-client zones and decoration hit testing verified.\n";
}

void test_window_movement_and_clamping() {
    std::cout << "[Test 2] Window movement via simulated drag and client frame update...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 200, 200);

    uint32_t wid = wm->create_window(100, "MoveApp", Rect{100, 100, 250, 180}, surf);
    auto win = wm->get_window(wid);
    TEST_ASSERT(win != nullptr);

    // Move window by dx = 40, dy = -30
    bool moved = wm->move_window(100, wid, 40, -30);
    TEST_ASSERT(moved);
    TEST_ASSERT(win->frame.x == 140);
    TEST_ASSERT(win->frame.y == 70);

    // Verify client frame translated accurately
    Rect cf = win->client_frame();
    TEST_ASSERT(cf.x == 140 + Window::kBorderWidth);
    TEST_ASSERT(cf.y == 70 + Window::kTitleBarHeight);

    // Attempt unauthorized move by foreign process (PID 999) must fail
    bool foreign_move = wm->move_window(999, wid, 20, 20);
    TEST_ASSERT(!foreign_move);
    (void)foreign_move;

    std::cout << "[PASS] Window movement and authorization verified.\n";
}

void test_z_order_and_focus_switching() {
    std::cout << "[Test 3] Overlapping windows Z-order elevation and hit-testing...\n";
    auto wm = std::make_shared<WindowManager>();

    auto surf1 = std::make_shared<DisplaySurface>(1, 100, 200, 200);
    auto surf2 = std::make_shared<DisplaySurface>(2, 200, 200, 200);

    // Window 1 at (50, 50, 200, 200)
    uint32_t wid1 = wm->create_window(100, "BackWin", Rect{50, 50, 200, 200}, surf1);
    // Window 2 at (100, 100, 200, 200) (overlaps Window 1)
    uint32_t wid2 = wm->create_window(200, "FrontWin", Rect{100, 100, 200, 200}, surf2);

    TEST_ASSERT(wm->focused_window_id() == wid2);

    // Coordinate (120, 120) is in both Window 1 and Window 2.
    // Frontmost window must be Window 2.
    auto hit = wm->hit_test(120, 120);
    TEST_ASSERT(hit != nullptr);
    TEST_ASSERT(hit->id == wid2);

    // Raise Window 1 to top and focus it
    TEST_ASSERT(wm->raise_to_top(wid1));
    TEST_ASSERT(wm->set_focus(wid1));
    TEST_ASSERT(wm->focused_window_id() == wid1);

    // Now coordinate (120, 120) hit-test must resolve to Window 1
    hit = wm->hit_test(120, 120);
    TEST_ASSERT(hit != nullptr);
    TEST_ASSERT(hit->id == wid1);

    std::cout << "[PASS] Z-order elevation and hit-testing verified.\n";
}

void test_window_close_and_destruction() {
    std::cout << "[Test 4] Window close and destruction cleanup...\n";
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 100, 200, 200);

    uint32_t wid = wm->create_window(100, "CloseApp", Rect{50, 50, 200, 200}, surf);
    TEST_ASSERT(wm->window_count() == 1);

    // Destroy window
    bool destroyed = wm->destroy_window(100, wid);
    TEST_ASSERT(destroyed);
    TEST_ASSERT(wm->window_count() == 0);
    TEST_ASSERT(wm->get_window(wid) == nullptr);

    std::cout << "[PASS] Window close and destruction verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.10: WINDOW INTERACTION TESTS\n";
    std::cout << "========================================\n";

    test_window_decorations_and_hit_testing();
    test_window_movement_and_clamping();
    test_z_order_and_focus_switching();
    test_window_close_and_destruction();

    std::cout << "\nALL STAGE 223.10 TESTS PASSED (100%).\n";
    return 0;
}
