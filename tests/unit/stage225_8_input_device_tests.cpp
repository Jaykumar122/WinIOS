#include <iostream>
#include <cstdint>
#include <memory>

#include "devices/input_device_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.8] Pointer & Keyboard Device Abstractions...\n" << std::flush;

    // 1. Pointer Device
    VirtualPointerDevice ptr;
    TEST_ASSERT(ptr.initialize());
    TEST_ASSERT(ptr.start());

    ptr.update_position(240, 360);
    ptr.set_button(0, true);  // Left button down
    ptr.set_button(1, true);  // Right button down
    ptr.set_wheel(120);
    ptr.set_captured(true);

    auto pst = ptr.get_state();
    TEST_ASSERT(pst.x == 240);
    TEST_ASSERT(pst.y == 360);
    TEST_ASSERT((pst.buttons & 0x1) != 0); // Left down
    TEST_ASSERT((pst.buttons & 0x2) != 0); // Right down
    TEST_ASSERT(pst.wheel_delta == 120);
    TEST_ASSERT(pst.is_captured);

    // Release all buttons (e.g. on focus loss or unregister)
    ptr.release_all_buttons();
    pst = ptr.get_state();
    TEST_ASSERT(pst.buttons == 0);

    // 2. Keyboard Device
    VirtualKeyboardDevice kbd;
    TEST_ASSERT(kbd.initialize());
    TEST_ASSERT(kbd.start());

    kbd.set_focus_window(10);
    kbd.set_key(0x41, true); // 'A' down
    kbd.set_key(0x10, true); // Shift down
    kbd.set_modifiers(0x01); // Shift modifier

    auto kst = kbd.get_state();
    TEST_ASSERT(kst.focused_window_id == 10);
    TEST_ASSERT(kst.pressed_key_count == 2);
    TEST_ASSERT(kbd.is_key_down(0x41));
    TEST_ASSERT(kbd.is_key_down(0x10));

    // Release all keys (e.g. on window blur or unregister)
    kbd.release_all_keys();
    kst = kbd.get_state();
    TEST_ASSERT(kst.pressed_key_count == 0);
    TEST_ASSERT(!kbd.is_key_down(0x41));
    TEST_ASSERT(!kbd.is_key_down(0x10));

    std::cout << "[Test Stage 225.8] PASS: Input devices verified.\n";
    return 0;
}
