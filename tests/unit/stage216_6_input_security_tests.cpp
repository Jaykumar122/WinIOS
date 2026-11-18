#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "graphics/window_manager.h"
#include "input/input_provenance.h"
#include "input/input_router.h"
#include "input/touch_event_queue.h"
#include "input/virtual_input_device.h"
#include "kernel/guest_trace_logger.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::input;
using namespace winios::graphics;

void test_cross_process_event_isolation() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf_alice = std::make_shared<DisplaySurface>(1, 1001, 100, 100);
    auto surf_bob   = std::make_shared<DisplaySurface>(2, 1002, 100, 100);
    std::uint32_t w_alice = win_mgr->create_window(1001, "Alice Window", Rect{10, 10, 100, 100}, surf_alice);
    std::uint32_t w_bob   = win_mgr->create_window(1002, "Bob Window", Rect{150, 10, 100, 100}, surf_bob);
    TEST_ASSERT(w_alice > 0 && w_bob > 0);

    // Send a secret keystroke to Alice
    win_mgr->set_focus(w_alice);
    InputEvent secret_key{};
    secret_key.type = InputEventType::KeyPress;
    secret_key.keyboard.virtual_key = 0x58; // 'X'
    TEST_ASSERT(router.route_event(secret_key));

    // Bob (PID 1002) attempts to poll Alice's event -> must receive nothing!
    InputEvent stolen_ev{};
    TEST_ASSERT(!router.poll_event(1002, stolen_ev));

    // Alice (PID 1001) polls her own event -> receives secret
    InputEvent alice_ev{};
    TEST_ASSERT(router.poll_event(1001, alice_ev));
    TEST_ASSERT(alice_ev.target_pid == 1001);
    TEST_ASSERT(alice_ev.keyboard.virtual_key == 0x58);

    std::cout << "[PASS] test_cross_process_event_isolation passed\n";
}

void test_destroyed_window_rejection() {
    auto win_mgr = std::make_shared<WindowManager>();
    InputRouter router(win_mgr);

    auto surf = std::make_shared<DisplaySurface>(5, 500, 100, 100);
    std::uint32_t wid = win_mgr->create_window(500, "Temp Window", Rect{10, 10, 100, 100}, surf);
    TEST_ASSERT(wid > 0);
    win_mgr->destroy_window(500, wid);

    auto& logger = kernel::GuestTraceLogger::instance();
    logger.reset();

    // Try routing an event targeting destroyed window ID
    InputEvent ev{};
    ev.type = InputEventType::MouseMove;
    ev.target_window_id = wid;
    ev.target_pid = 500;
    ev.mouse.x = 20;
    ev.mouse.y = 20;

    // WindowManager no longer has wid
    bool routed = router.route_event(ev);
    TEST_ASSERT(!routed || logger.input_provenance_records().back().delivery_status == InputDeliveryStatus::RejectedWindowDestroyed);

    std::cout << "[PASS] test_destroyed_window_rejection passed\n";
}

void test_queue_overflow_protection() {
    TouchEventQueue q(16);
    TEST_ASSERT(q.capacity() == 16);

    for (std::size_t i = 0; i < 32; ++i) {
        InputEvent ev{};
        ev.type = InputEventType::MouseMove;
        ev.mouse.x = static_cast<std::int32_t>(i);
        q.push(ev);
    }

    TEST_ASSERT(q.full());
    TEST_ASSERT(q.size() == 16);
    TEST_ASSERT(q.overflow_count() == 16);
    TEST_ASSERT(q.total_pushed() == 32);

    // Oldest 16 events were dropped, the remaining 16 are 16..31
    InputEvent popped{};
    TEST_ASSERT(q.pop(popped));
    TEST_ASSERT(popped.mouse.x == 16);

    std::cout << "[PASS] test_queue_overflow_protection passed\n";
}

void test_provenance_audit_trail_distinction() {
    auto& logger = kernel::GuestTraceLogger::instance();
    logger.reset();

    // 1. Genuine physical input
    InputEvent physical_ev{};
    physical_ev.type = InputEventType::MouseDown;
    physical_ev.provenance = InputProvenance::PhysicalWindowsInput;
    physical_ev.win32_msg_id = 0x0201;
    physical_ev.target_pid = 100;
    logger.log_input_provenance(physical_ev, InputDeliveryStatus::Delivered);

    // 2. Synthetic injected message
    InputEvent injected_ev{};
    injected_ev.type = InputEventType::MouseDown;
    injected_ev.provenance = InputProvenance::WindowsMessageInjected;
    injected_ev.win32_msg_id = 0x0201;
    injected_ev.target_pid = 100;
    logger.log_input_provenance(injected_ev, InputDeliveryStatus::Delivered);

    const auto& recs = logger.input_provenance_records();
    TEST_ASSERT(recs.size() == 2);
    TEST_ASSERT(recs[0].provenance == InputProvenance::PhysicalWindowsInput);
    TEST_ASSERT(recs[1].provenance == InputProvenance::WindowsMessageInjected);
    TEST_ASSERT(recs[0].provenance != recs[1].provenance);

    std::cout << "[PASS] test_provenance_audit_trail_distinction passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.6 Input Security Tests ===" << std::endl;
    test_cross_process_event_isolation();
    test_destroyed_window_rejection();
    test_queue_overflow_protection();
    test_provenance_audit_trail_distinction();
    std::cout << "=== All Stage 216.6 Input Security Tests PASSED ===" << std::endl;
    return 0;
}
