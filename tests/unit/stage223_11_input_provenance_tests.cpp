#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#include "input/input_provenance.h"
#include "input/input_router.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/interactive_runtime_contract.h"

using namespace winios;
using namespace winios::input;
using namespace winios::graphics;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_provenance_classification_and_strings() {
    std::cout << "[Test 1] Provenance classifications and telemetry string formatting...\n";

    TEST_ASSERT(std::string(input_provenance_to_string(InputProvenance::PhysicalWindowsInput)) == "PhysicalWindowsInput");
    TEST_ASSERT(std::string(input_provenance_to_string(InputProvenance::WindowsMessageInjected)) == "WindowsMessageInjected");
    TEST_ASSERT(std::string(input_provenance_to_string(InputProvenance::TestInjected)) == "TestInjected");
    TEST_ASSERT(std::string(input_provenance_to_string(InputProvenance::GuestGenerated)) == "GuestGenerated");
    TEST_ASSERT(std::string(input_provenance_to_string(InputProvenance::SyntheticHostEvent)) == "SyntheticHostEvent");

    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::Delivered)) == "Delivered");
    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::DroppedQueueFull)) == "DroppedQueueFull");
    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::RejectedNoTargetWindow)) == "RejectedNoTargetWindow");
    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::RejectedProcessMismatch)) == "RejectedProcessMismatch");
    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::RejectedWindowDestroyed)) == "RejectedWindowDestroyed");
    TEST_ASSERT(std::string(input_delivery_status_to_string(InputDeliveryStatus::RejectedUnauthorized)) == "RejectedUnauthorized");

    std::cout << "[PASS] Provenance strings and enums verified.\n";
}

void test_embedded_provenance_telemetry_structure() {
    std::cout << "[Test 2] Validating winios_event_v2_t provenance layout and contract...\n";

    TEST_ASSERT(sizeof(winios_input_provenance_t) == 24);
    TEST_ASSERT(sizeof(winios_event_v2_t) == 64);

    winios_event_v2_t evt{};
    evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    evt.contact_id = 1;
    evt.x = 100;
    evt.y = 150;
    evt.window_id = 5;
    evt.timestamp_us = 123456789ULL;

    evt.provenance.device_type = 1; // Mouse
    evt.provenance.device_id = 0xAA;
    evt.provenance.host_timestamp = 123456780ULL;
    evt.provenance.sequence_number = 42;
    evt.provenance.flags = 1; // Physical

    TEST_ASSERT(evt.provenance.sequence_number == 42);
    TEST_ASSERT(evt.provenance.flags == 1);
    TEST_ASSERT(evt.timestamp_us >= evt.provenance.host_timestamp);

    std::cout << "[PASS] Embedded provenance layout verified.\n";
}

void test_mouse_capture_and_routing() {
    std::cout << "[Test 3] InputRouter hit testing, drag capture and release...\n";

    auto wm = std::make_shared<WindowManager>();
    InputRouter router(wm);

    auto surfA = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    auto surfB = std::make_shared<DisplaySurface>(2, 20, 200, 200);

    uint32_t widA = wm->create_window(10, "AppA", Rect{0, 0, 200, 200}, surfA);
    uint32_t widB = wm->create_window(20, "AppB", Rect{150, 0, 200, 200}, surfB);

    // Initial click at (50, 50) -> Inside AppA (PID 10)
    InputEvent ev1{};
    ev1.type = InputEventType::MouseDown;
    ev1.mouse.x = 50;
    ev1.mouse.y = 50;
    ev1.mouse.button = MouseButton::Left;
    bool routed = router.route_event(ev1);
    TEST_ASSERT(routed);
    TEST_ASSERT(router.is_mouse_captured());
    TEST_ASSERT(router.mouse_captured_pid() == 10);
    TEST_ASSERT(router.mouse_captured_window_id() == widA);

    // While captured, drag to (180, 50) which is geographically over AppB,
    // but MUST still route to AppA because of active mouse capture.
    InputEvent ev2{};
    ev2.type = InputEventType::MouseMove;
    ev2.mouse.x = 180;
    ev2.mouse.y = 50;
    routed = router.route_event(ev2);
    TEST_ASSERT(routed);
    TEST_ASSERT(router.pending_events_for_process(10) == 2);
    TEST_ASSERT(router.pending_events_for_process(20) == 0);

    // Release mouse button -> Capture must clear
    InputEvent ev3{};
    ev3.type = InputEventType::MouseUp;
    ev3.mouse.x = 180;
    ev3.mouse.y = 50;
    ev3.mouse.button = MouseButton::Left;
    routed = router.route_event(ev3);
    TEST_ASSERT(routed);
    TEST_ASSERT(!router.is_mouse_captured());

    // Window destruction clears associated queues
    router.on_window_destroyed(widA);
    router.on_window_destroyed(widB);

    std::cout << "[PASS] Mouse capture and routing verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.11: INPUT PROVENANCE TESTS\n";
    std::cout << "========================================\n";

    test_provenance_classification_and_strings();
    test_embedded_provenance_telemetry_structure();
    test_mouse_capture_and_routing();

    std::cout << "\nALL STAGE 223.11 TESTS PASSED (100%).\n";
    return 0;
}
