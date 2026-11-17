// ============================================================================
// Stage 213.7: Window and Surface Manager Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"

using namespace winios::graphics;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_window_creation_and_attributes() {
    std::cout << "[Test 1] Verify Window Creation, Title, Frame and Surface Association...\n";

    WindowManager wm;
    auto surf = std::make_shared<DisplaySurface>(1, 10, 400, 300);

    Rect frame{50, 100, 400, 300};
    std::uint32_t wid = wm.create_window(10, "SpringBoard Shell", frame, surf);

    TEST_CHECK(wid > 0, "Window created with valid ID");
    TEST_CHECK(wm.window_count() == 1, "Window count is 1");

    auto win = wm.get_window(wid);
    TEST_CHECK(win != nullptr, "get_window returns valid pointer");
    TEST_CHECK(win->owner_pid == 10, "Owner PID is 10");
    TEST_CHECK(win->title == "SpringBoard Shell", "Title matches");
    TEST_CHECK(win->frame.x == 50 && win->frame.y == 100, "Frame position matches");
    TEST_CHECK(win->surface == surf, "Surface associated correctly");
    TEST_CHECK(win->is_visible(), "Window is visible");
    TEST_CHECK(win->is_focused, "New window has focus by default");
    TEST_CHECK(wm.focused_window_id() == wid, "focused_window_id matches wid");
}

void test_focus_and_z_order_reassignment() {
    std::cout << "[Test 2] Verify Focus Management, Z-Ordering and Destruction Focus Transfer...\n";

    WindowManager wm;
    auto surf1 = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surf2 = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    std::uint32_t w1 = wm.create_window(10, "App 1", Rect{0, 0, 200, 200}, surf1);
    std::uint32_t w2 = wm.create_window(20, "App 2", Rect{50, 50, 200, 200}, surf2);

    TEST_CHECK(wm.focused_window_id() == w2, "Window 2 has focus after creation");
    TEST_CHECK(wm.focused_pid() == 20, "Focused PID is 20");

    // Switch focus back to Window 1
    TEST_CHECK(wm.set_focus(w1), "set_focus to w1 succeeds");
    TEST_CHECK(wm.focused_window_id() == w1, "Window 1 now has focus");
    TEST_CHECK(wm.focused_pid() == 10, "Focused PID is 10");

    // Verify Z-ordering: App 2 was created second so it had higher z by default
    wm.set_window_z_order(10, w1, 100);
    wm.set_window_z_order(20, w2, 50);

    auto ordered = wm.get_visible_windows_z_ordered();
    TEST_CHECK(ordered.size() == 2, "2 visible windows");
    TEST_CHECK(ordered[0]->id == w2, "Bottom window is w2 (z=50)");
    TEST_CHECK(ordered[1]->id == w1, "Top window is w1 (z=100)");

    // Destroy focused window w1 -> focus must transfer to w2
    TEST_CHECK(wm.destroy_window(10, w1), "Destroying w1 succeeds");
    TEST_CHECK(wm.focused_window_id() == w2, "Focus automatically transferred to remaining visible window w2");
}

void test_hit_testing() {
    std::cout << "[Test 3] Verify Frontmost Hit Testing with Overlapping Windows...\n";

    WindowManager wm;
    auto surf1 = std::make_shared<DisplaySurface>(1, 1, 300, 300);
    auto surf2 = std::make_shared<DisplaySurface>(2, 2, 200, 200);

    // Window 1: [0, 0, 300, 300], Z=10
    std::uint32_t w1 = wm.create_window(1, "Back Window", Rect{0, 0, 300, 300}, surf1);
    wm.set_window_z_order(1, w1, 10);

    // Window 2: [50, 50, 200, 200], Z=20 (on top)
    std::uint32_t w2 = wm.create_window(2, "Front Window", Rect{50, 50, 200, 200}, surf2);
    wm.set_window_z_order(2, w2, 20);

    // Hit at (100, 100): Inside both w1 and w2. Must return w2 (higher Z)
    auto hit = wm.hit_test(100, 100);
    TEST_CHECK(hit != nullptr && hit->id == w2, "Hit test at (100, 100) selects frontmost window w2");

    // Hit at (20, 20): Inside w1 only
    hit = wm.hit_test(20, 20);
    TEST_CHECK(hit != nullptr && hit->id == w1, "Hit test at (20, 20) selects back window w1");

    // Hit at (500, 500): Outside both windows
    hit = wm.hit_test(500, 500);
    TEST_CHECK(hit == nullptr, "Hit test outside all windows returns nullptr");
}

void test_process_window_cleanup() {
    std::cout << "[Test 4] Verify Process Window Cleanup upon Termination...\n";

    WindowManager wm;
    wm.create_window(33, "P33 Win 1", Rect{0, 0, 100, 100}, std::make_shared<DisplaySurface>(1, 33, 100, 100));
    wm.create_window(33, "P33 Win 2", Rect{100, 0, 100, 100}, std::make_shared<DisplaySurface>(2, 33, 100, 100));
    wm.create_window(44, "P44 Win 1", Rect{0, 100, 100, 100}, std::make_shared<DisplaySurface>(3, 44, 100, 100));

    TEST_CHECK(wm.window_count() == 3, "3 windows total");
    TEST_CHECK(wm.get_windows_for_process(33).size() == 2, "PID 33 owns 2 windows");

    wm.cleanup_process_windows(33);

    TEST_CHECK(wm.window_count() == 1, "Only 1 window remaining after PID 33 cleanup");
    TEST_CHECK(wm.get_windows_for_process(33).empty(), "PID 33 windows completely removed");
    TEST_CHECK(wm.get_windows_for_process(44).size() == 1, "PID 44 window intact");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.7: Window Manager Tests           \n";
    std::cout << "====================================================\n";

    test_window_creation_and_attributes();
    test_focus_and_z_order_reassignment();
    test_hit_testing();
    test_process_window_cleanup();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
