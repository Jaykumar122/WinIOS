// ============================================================================
// Stage 213.13: Virtual Input Device Architecture Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "input/virtual_input_device.h"

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

void test_touch_contact_lifecycle() {
    std::cout << "[Test 1] Verify Multi-Touch Contact Begin, Move, End and Cancel...\n";

    VirtualInputDevice dev(1);
    TEST_CHECK(dev.active_contact_count() == 0, "Initial active contacts count is 0");

    std::vector<InputEvent> received_events;
    dev.set_dispatch_callback([&](const InputEvent& evt) {
        received_events.push_back(evt);
    });

    // TouchBegin for contact 0 at (100, 200)
    TEST_CHECK(dev.inject_touch_begin(0, 100, 200, 1000ULL), "inject_touch_begin for contact 0 succeeds");
    TEST_CHECK(dev.active_contact_count() == 1, "Active contacts count is 1");
    TEST_CHECK(dev.is_contact_active(0), "Contact 0 is active");
    TEST_CHECK(dev.get_contact(0).x == 100 && dev.get_contact(0).y == 200, "Contact 0 coordinates match (100, 200)");

    // TouchBegin for contact 1 at (300, 400)
    TEST_CHECK(dev.inject_touch_begin(1, 300, 400, 1500ULL), "inject_touch_begin for contact 1 succeeds");
    TEST_CHECK(dev.active_contact_count() == 2, "Active contacts count is 2 (multi-touch)");

    // TouchMove for contact 0 to (120, 220)
    TEST_CHECK(dev.inject_touch_move(0, 120, 220, 2000ULL), "inject_touch_move for contact 0 succeeds");
    TEST_CHECK(dev.get_contact(0).x == 120 && dev.get_contact(0).y == 220, "Contact 0 coordinates updated to (120, 220)");

    // TouchEnd for contact 0
    TEST_CHECK(dev.inject_touch_end(0, 3000ULL), "inject_touch_end for contact 0 succeeds");
    TEST_CHECK(!dev.is_contact_active(0), "Contact 0 is inactive");
    TEST_CHECK(dev.active_contact_count() == 1, "Active contacts count is 1");

    // TouchCancel for contact 1
    TEST_CHECK(dev.inject_touch_cancel(1, 4000ULL), "inject_touch_cancel for contact 1 succeeds");
    TEST_CHECK(!dev.is_contact_active(1), "Contact 1 is inactive");
    TEST_CHECK(dev.active_contact_count() == 0, "Active contacts count is 0");

    // Verify dispatched event types
    TEST_CHECK(received_events.size() == 5, "Total 5 events dispatched");
    TEST_CHECK(received_events[0].type == InputEventType::TouchBegin, "Event 0 is TouchBegin");
    TEST_CHECK(received_events[1].type == InputEventType::TouchBegin, "Event 1 is TouchBegin");
    TEST_CHECK(received_events[2].type == InputEventType::TouchMove, "Event 2 is TouchMove");
    TEST_CHECK(received_events[3].type == InputEventType::TouchEnd, "Event 3 is TouchEnd");
    TEST_CHECK(received_events[4].type == InputEventType::TouchCancel, "Event 4 is TouchCancel");
}

void test_hardware_buttons_and_keys() {
    std::cout << "[Test 2] Verify Home Button, Lock Button and Keyboard Injection...\n";

    VirtualInputDevice dev(2);
    std::vector<InputEvent> received;
    dev.set_dispatch_callback([&](const InputEvent& evt) {
        received.push_back(evt);
    });

    dev.inject_home_button(10000ULL);
    dev.inject_lock_button(20000ULL);
    dev.inject_key(0x20, true, 30000ULL);  // Space key down
    dev.inject_key(0x20, false, 35000ULL); // Space key up

    TEST_CHECK(received.size() == 4, "4 events received");
    TEST_CHECK(received[0].type == InputEventType::ButtonHome, "First event is Home Button");
    TEST_CHECK(received[1].type == InputEventType::ButtonLock, "Second event is Lock Button");
    TEST_CHECK(received[2].type == InputEventType::KeyPress && received[2].key_code == 0x20, "Third event is KeyPress Space");
    TEST_CHECK(received[3].type == InputEventType::KeyRelease && received[3].key_code == 0x20, "Fourth event is KeyRelease Space");
}

void test_contact_bounds_and_reset() {
    std::cout << "[Test 3] Verify Contact ID Bounds Protection and Device Reset...\n";

    VirtualInputDevice dev(3);

    // Contact ID 10 (max is 9) rejected
    TEST_CHECK(!dev.inject_touch_begin(10, 50, 50, 100ULL), "Contact ID 10 out of bounds rejected");
    TEST_CHECK(!dev.inject_touch_move(10, 50, 50, 100ULL), "Contact ID 10 move rejected");
    TEST_CHECK(!dev.inject_touch_end(10, 100ULL), "Contact ID 10 end rejected");

    // Activate contacts 0, 1, 2
    dev.inject_touch_begin(0, 10, 10, 100ULL);
    dev.inject_touch_begin(1, 20, 20, 100ULL);
    dev.inject_touch_begin(2, 30, 30, 100ULL);
    TEST_CHECK(dev.active_contact_count() == 3, "3 contacts active");

    dev.reset();
    TEST_CHECK(dev.active_contact_count() == 0, "Active contacts reset to 0");
    TEST_CHECK(!dev.is_contact_active(0), "Contact 0 inactive after reset");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.13: Virtual Input Device Tests    \n";
    std::cout << "====================================================\n";

    test_touch_contact_lifecycle();
    test_hardware_buttons_and_keys();
    test_contact_bounds_and_reset();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
