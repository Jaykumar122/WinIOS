#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "input/input_provenance.h"
#include "input/virtual_input_device.h"
#include "kernel/guest_trace_logger.h"
#include "ui/display_window.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::input;

void test_provenance_string_conversions() {
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

    std::cout << "[PASS] test_provenance_string_conversions passed\n";
}

void test_provenance_telemetry_capture() {
    auto& logger = kernel::GuestTraceLogger::instance();
    logger.reset();
    TEST_ASSERT(logger.input_provenance_records().empty());

    // 1. Log a physical input event
    InputEvent ev1{};
    ev1.type = InputEventType::MouseMove;
    ev1.provenance = InputProvenance::PhysicalWindowsInput;
    ev1.win32_msg_id = 0x0200; // WM_MOUSEMOVE
    ev1.mouse.screen_x = 100;
    ev1.mouse.screen_y = 150;
    ev1.mouse.client_x = 50;
    ev1.mouse.client_y = 75;
    ev1.mouse.x = 200;
    ev1.mouse.y = 300;
    ev1.target_pid = 42;
    ev1.target_window_id = 101;

    logger.log_input_provenance(ev1, InputDeliveryStatus::Delivered);
    TEST_ASSERT(logger.input_provenance_records().size() == 1);

    const auto& rec1 = logger.input_provenance_records().back();
    TEST_ASSERT(rec1.sequence_number == 1);
    TEST_ASSERT(rec1.provenance == InputProvenance::PhysicalWindowsInput);
    TEST_ASSERT(rec1.win32_msg_id == 0x0200);
    TEST_ASSERT(rec1.target_pid == 42);
    TEST_ASSERT(rec1.target_window_id == 101);
    TEST_ASSERT(rec1.client_x == 50);
    TEST_ASSERT(rec1.client_y == 75);
    TEST_ASSERT(rec1.guest_x == 200);
    TEST_ASSERT(rec1.guest_y == 300);
    TEST_ASSERT(rec1.delivery_status == InputDeliveryStatus::Delivered);

    // 2. Log an injected Windows message event
    InputEvent ev2{};
    ev2.type = InputEventType::MouseDown;
    ev2.provenance = InputProvenance::WindowsMessageInjected;
    ev2.win32_msg_id = 0x0201; // WM_LBUTTONDOWN
    ev2.mouse.button = MouseButton::Left;
    ev2.target_pid = 42;
    ev2.target_window_id = 101;

    logger.log_input_provenance(ev2, InputDeliveryStatus::Delivered);
    TEST_ASSERT(logger.input_provenance_records().size() == 2);
    const auto& rec2 = logger.input_provenance_records().back();
    TEST_ASSERT(rec2.sequence_number == 2);
    TEST_ASSERT(rec2.provenance == InputProvenance::WindowsMessageInjected);

    // 3. Log a test-injected event with rejection status
    InputEvent ev3{};
    ev3.type = InputEventType::KeyPress;
    ev3.provenance = InputProvenance::TestInjected;
    ev3.win32_msg_id = 0x0100;
    ev3.target_pid = 999;
    ev3.target_window_id = 888;

    logger.log_input_provenance(ev3, InputDeliveryStatus::RejectedWindowDestroyed);
    TEST_ASSERT(logger.input_provenance_records().size() == 3);
    const auto& rec3 = logger.input_provenance_records().back();
    TEST_ASSERT(rec3.sequence_number == 3);
    TEST_ASSERT(rec3.provenance == InputProvenance::TestInjected);
    TEST_ASSERT(rec3.delivery_status == InputDeliveryStatus::RejectedWindowDestroyed);

    std::cout << "[PASS] test_provenance_telemetry_capture passed\n";
}

void test_display_window_provenance_injection() {
    ui::DisplayWindowConfig cfg;
    cfg.headless = true;
    cfg.native_width = 400;
    cfg.native_height = 800;
    ui::DisplayWindow win(cfg);
    TEST_ASSERT(win.create());

    auto& logger = kernel::GuestTraceLogger::instance();
    logger.reset();

    // Verify inject_mouse_move with TestInjected
    win.inject_mouse_move(120, 240, InputProvenance::TestInjected);
    TEST_ASSERT(logger.input_provenance_records().size() == 1);
    TEST_ASSERT(logger.input_provenance_records().back().provenance == InputProvenance::TestInjected);
    TEST_ASSERT(logger.input_provenance_records().back().guest_x == 120);
    TEST_ASSERT(logger.input_provenance_records().back().guest_y == 240);

    // Verify inject_mouse_button
    win.inject_mouse_button(MouseButton::Right, true, 120, 240, InputProvenance::WindowsMessageInjected);
    TEST_ASSERT(logger.input_provenance_records().size() == 2);
    TEST_ASSERT(logger.input_provenance_records().back().provenance == InputProvenance::WindowsMessageInjected);

    // Verify inject_key_event
    win.inject_key_event(65, true, false, 0, InputProvenance::PhysicalWindowsInput);
    TEST_ASSERT(logger.input_provenance_records().size() == 3);
    TEST_ASSERT(logger.input_provenance_records().back().provenance == InputProvenance::PhysicalWindowsInput);

    // Verify inject_char_event
    win.inject_char_event(U'A', InputProvenance::TestInjected);
    TEST_ASSERT(logger.input_provenance_records().size() == 4);
    TEST_ASSERT(logger.input_provenance_records().back().provenance == InputProvenance::TestInjected);

    std::cout << "[PASS] test_display_window_provenance_injection passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.2 Input Provenance Tests ===" << std::endl;
    test_provenance_string_conversions();
    test_provenance_telemetry_capture();
    test_display_window_provenance_injection();
    std::cout << "=== All Stage 216.2 Input Provenance Tests PASSED ===" << std::endl;
    return 0;
}
