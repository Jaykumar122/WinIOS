// ============================================================================
// Stage 213.16: Guest Input API and Per-Process Delivery Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "input/input_router.h"

using namespace winios::graphics;
using namespace winios::input;

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

void test_per_process_event_isolation() {
    std::cout << "[Test 1] Verify Per-Process Event Queuing and Process Isolation...\n";

    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    // Create 2 windows owned by PID 10 and PID 20
    auto surf1 = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surf2 = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    std::uint32_t w1 = wm->create_window(10, "App 1", Rect{0, 0, 200, 200}, surf1);
    std::uint32_t w2 = wm->create_window(20, "App 2", Rect{250, 0, 200, 200}, surf2);

    // Route event targeted at App 1 (x=50, y=50 inside w1)
    InputEvent e1{};
    e1.type = InputEventType::TouchBegin;
    e1.touch.contact_id = 0;
    e1.touch.x = 50;
    e1.touch.y = 50;
    e1.timestamp_us = 1000;
    TEST_CHECK(router.route_event(e1), "Event e1 routed successfully");

    // Route event targeted at App 2 (x=300, y=50 inside w2)
    InputEvent e2{};
    e2.type = InputEventType::TouchBegin;
    e2.touch.contact_id = 1;
    e2.touch.x = 300;
    e2.touch.y = 50;
    e2.timestamp_us = 2000;
    TEST_CHECK(router.route_event(e2), "Event e2 routed successfully");

    // Process 10 should receive ONLY e1
    InputEvent polled10{};
    TEST_CHECK(router.poll_event(10, polled10), "PID 10 polled event");
    TEST_CHECK(polled10.touch.contact_id == 0 && polled10.touch.x == 50, "PID 10 received e1");
    TEST_CHECK(polled10.target_window_id == w1, "Target window matches w1");
    TEST_CHECK(!router.poll_event(10, polled10), "PID 10 queue is now empty (cannot see PID 20's event)");

    // Process 20 should receive ONLY e2
    InputEvent polled20{};
    TEST_CHECK(router.poll_event(20, polled20), "PID 20 polled event");
    TEST_CHECK(polled20.touch.contact_id == 1 && polled20.touch.x == 300, "PID 20 received e2");
    TEST_CHECK(polled20.target_window_id == w2, "Target window matches w2");
    TEST_CHECK(!router.poll_event(20, polled20), "PID 20 queue is now empty");

    // Unauthorized third process PID 99 should have empty queue
    InputEvent polled99{};
    TEST_CHECK(!router.poll_event(99, polled99), "PID 99 receives no events");
}

void test_touch_contact_capture_lifecycle() {
    std::cout << "[Test 2] Verify Touch Contact Capture on Begin and Release on End...\n";

    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surf = std::make_shared<DisplaySurface>(1, 15, 300, 300);
    std::uint32_t wid = wm->create_window(15, "Target App", Rect{100, 100, 300, 300}, surf);

    // 1. TouchBegin at (150, 150) -> captures contact 0 to window wid / PID 15
    InputEvent evt_begin{};
    evt_begin.type = InputEventType::TouchBegin;
    evt_begin.touch.contact_id = 0;
    evt_begin.touch.x = 150;
    evt_begin.touch.y = 150;
    router.route_event(evt_begin);

    // 2. TouchMove to (500, 500) -> OUTSIDE window frame!
    // Because contact 0 was captured by wid on TouchBegin, move MUST still be routed to PID 15!
    InputEvent evt_move{};
    evt_move.type = InputEventType::TouchMove;
    evt_move.touch.contact_id = 0;
    evt_move.touch.x = 500;
    evt_move.touch.y = 500;
    router.route_event(evt_move);

    // 3. TouchEnd
    InputEvent evt_end{};
    evt_end.type = InputEventType::TouchEnd;
    evt_end.touch.contact_id = 0;
    evt_end.touch.x = 500;
    evt_end.touch.y = 500;
    router.route_event(evt_end);

    // Verify all 3 events reached PID 15 in order
    InputEvent p{};
    TEST_CHECK(router.poll_event(15, p) && p.type == InputEventType::TouchBegin, "Event 1 is TouchBegin");
    TEST_CHECK(router.poll_event(15, p) && p.type == InputEventType::TouchMove && p.touch.x == 500, "Event 2 is TouchMove");
    TEST_CHECK(router.poll_event(15, p) && p.type == InputEventType::TouchEnd, "Event 3 is TouchEnd");
    TEST_CHECK(!router.poll_event(15, p), "Queue empty after end");

    // Now contact 0 is released. Another TouchMove with contact 0 should not route
    InputEvent stale_move = evt_move;
    TEST_CHECK(!router.route_event(stale_move), "Stale TouchMove without active capture rejected");
}

void test_window_destruction_touch_cancellation() {
    std::cout << "[Test 3] Verify Automatic Touch Cancellation on Window Destruction...\n";

    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surf = std::make_shared<DisplaySurface>(1, 50, 200, 200);
    std::uint32_t wid = wm->create_window(50, "App", Rect{0, 0, 200, 200}, surf);

    // TouchBegin capturing contact 0
    InputEvent b{};
    b.type = InputEventType::TouchBegin;
    b.touch.contact_id = 0;
    b.touch.x = 50;
    b.touch.y = 50;
    router.route_event(b);

    // Consume TouchBegin
    InputEvent out{};
    router.poll_event(50, out);

    // Window destroyed while touch active
    router.on_window_destroyed(wid);

    // Verify TouchCancel was synthesized to PID 50
    TEST_CHECK(router.poll_event(50, out), "Synthesized event delivered after window destruction");
    TEST_CHECK(out.type == InputEventType::TouchCancel, "Event is TouchCancel");
    TEST_CHECK(out.touch.contact_id == 0, "Cancelled contact is 0");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.16: Guest Input API Tests         \n";
    std::cout << "====================================================\n";

    test_per_process_event_isolation();
    test_touch_contact_capture_lifecycle();
    test_window_destruction_touch_cancellation();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
