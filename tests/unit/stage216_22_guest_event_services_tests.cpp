#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "runtime/guest_event_services.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::runtime;

void test_timer_creation_and_quota() {
    auto wm = std::make_shared<WindowManager>();
    auto router = std::make_shared<InputRouter>(wm);
    GuestEventServices ev_svc(router);

    TEST_ASSERT(ev_svc.active_timer_count() == 0);

    // Invalid interval
    TEST_ASSERT(ev_svc.create_timer(10, 0, false) == 0);

    // Create 16 timers for PID 10 (hits quota limit)
    std::vector<std::uint32_t> tids;
    for (int i = 0; i < 16; ++i) {
        std::uint32_t tid = ev_svc.create_timer(10, 100 * (i + 1), true);
        TEST_ASSERT(tid != 0);
        tids.push_back(tid);
    }

    TEST_ASSERT(ev_svc.active_timers_for_process(10) == 16);

    // 17th timer should fail due to quota limit
    TEST_ASSERT(ev_svc.create_timer(10, 50, true) == 0);

    // Different PID (20) should still be able to create timers
    std::uint32_t pid20_t1 = ev_svc.create_timer(20, 50, false);
    TEST_ASSERT(pid20_t1 != 0);
    TEST_ASSERT(ev_svc.active_timers_for_process(20) == 1);

    // Cancel timer
    TEST_ASSERT(ev_svc.cancel_timer(10, tids[0]));
    TEST_ASSERT(ev_svc.active_timers_for_process(10) == 15);

    // Cross-PID cancel should fail
    TEST_ASSERT(!ev_svc.cancel_timer(20, tids[1]));

    // Cleanup process timers
    ev_svc.cleanup_process_timers(10);
    TEST_ASSERT(ev_svc.active_timers_for_process(10) == 0);
    TEST_ASSERT(ev_svc.active_timers_for_process(20) == 1);

    std::cout << "[PASS] test_timer_creation_and_quota\n";
}

void test_timer_virtual_stepping_and_delivery() {
    auto wm = std::make_shared<WindowManager>();
    auto router = std::make_shared<InputRouter>(wm);
    GuestEventServices ev_svc(router);

    // Register a one-shot timer (100ms) and a periodic timer (50ms) for PID 5
    std::uint32_t t_oneshot = ev_svc.create_timer(5, 100, false);
    std::uint32_t t_periodic = ev_svc.create_timer(5, 50, true);
    TEST_ASSERT(t_oneshot != 0 && t_periodic != 0);

    // Step 20ms: neither should fire yet
    ev_svc.step(20);
    TEST_ASSERT(ev_svc.current_virtual_time_ms() == 20);
    InputEvent ev{};
    TEST_ASSERT(!router->poll_event(5, ev));

    // Step another 30ms (total 50ms): periodic timer should fire!
    ev_svc.step(30);
    TEST_ASSERT(ev_svc.current_virtual_time_ms() == 50);
    TEST_ASSERT(router->poll_event(5, ev));
    TEST_ASSERT(ev.type == InputEventType::TimerTick);
    TEST_ASSERT(ev.key_code == t_periodic);
    TEST_ASSERT(ev.provenance == InputProvenance::SyntheticHostEvent);

    // No other event currently pending
    TEST_ASSERT(!router->poll_event(5, ev));

    // Step another 50ms (total 100ms): both periodic (2nd tick) and one-shot (1st tick) should fire!
    ev_svc.step(50);
    TEST_ASSERT(ev_svc.current_virtual_time_ms() == 100);

    bool got_oneshot = false;
    bool got_periodic = false;
    while (router->poll_event(5, ev)) {
        TEST_ASSERT(ev.type == InputEventType::TimerTick);
        if (ev.key_code == t_oneshot) got_oneshot = true;
        if (ev.key_code == t_periodic) got_periodic = true;
    }
    TEST_ASSERT(got_oneshot);
    TEST_ASSERT(got_periodic);

    // One-shot timer should now be inactive and removed
    TEST_ASSERT(ev_svc.active_timers_for_process(5) == 1);

    // Step another 50ms (total 150ms): periodic should fire 3rd time, one-shot should NOT fire
    ev_svc.step(50);
    TEST_ASSERT(router->poll_event(5, ev));
    TEST_ASSERT(ev.key_code == t_periodic);
    TEST_ASSERT(!router->poll_event(5, ev));

    std::cout << "[PASS] test_timer_virtual_stepping_and_delivery\n";
}

int main() {
    std::cout << "Starting Stage 216.22 Guest Event Services Tests...\n";
    test_timer_creation_and_quota();
    test_timer_virtual_stepping_and_delivery();
    std::cout << "All Stage 216.22 Tests Passed Successfully!\n";
    return 0;
}
