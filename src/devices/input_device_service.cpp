#include "devices/input_device_service.h"
#include "security/capability_manager.h"

namespace winios::devices {

// -----------------------------------------------------------------------------
// VirtualPointerDevice
// -----------------------------------------------------------------------------
VirtualPointerDevice::VirtualPointerDevice()
    : VirtualDeviceBase(2, 1, DeviceClass::PointerInput, "Virtual Pointer Device",
                        "WinIOS.Input.Pointer", 0x00010000,
                        static_cast<std::uint32_t>(security::Capability::DeviceInput)) {
}

void VirtualPointerDevice::update_position(std::int32_t x, std::int32_t y) {
    std::lock_guard<std::mutex> guard(lock_);
    pointer_state_.x = x;
    pointer_state_.y = y;
}

void VirtualPointerDevice::set_button(std::uint8_t button_index, bool down) {
    std::lock_guard<std::mutex> guard(lock_);
    if (button_index < 32) {
        if (down) {
            pointer_state_.buttons |= (1u << button_index);
        } else {
            pointer_state_.buttons &= ~(1u << button_index);
        }
    }
}

void VirtualPointerDevice::set_wheel(std::int32_t delta) {
    std::lock_guard<std::mutex> guard(lock_);
    pointer_state_.wheel_delta = delta;
}

void VirtualPointerDevice::set_captured(bool captured) {
    std::lock_guard<std::mutex> guard(lock_);
    pointer_state_.is_captured = captured;
}

void VirtualPointerDevice::release_all_buttons() {
    std::lock_guard<std::mutex> guard(lock_);
    pointer_state_.buttons = 0;
    pointer_state_.wheel_delta = 0;
    pointer_state_.is_captured = false;
}

PointerStateData VirtualPointerDevice::get_state() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pointer_state_;
}

// -----------------------------------------------------------------------------
// VirtualKeyboardDevice
// -----------------------------------------------------------------------------
VirtualKeyboardDevice::VirtualKeyboardDevice()
    : VirtualDeviceBase(3, 1, DeviceClass::KeyboardInput, "Virtual Keyboard Device",
                        "WinIOS.Input.Keyboard", 0x00010000,
                        static_cast<std::uint32_t>(security::Capability::DeviceInput)) {
}

void VirtualKeyboardDevice::set_key(std::uint8_t vkey, bool down) {
    std::lock_guard<std::mutex> guard(lock_);
    const bool was_down = (keyboard_state_.key_states[vkey] != 0);
    if (down && !was_down) {
        keyboard_state_.key_states[vkey] = 1;
        ++keyboard_state_.pressed_key_count;
    } else if (!down && was_down) {
        keyboard_state_.key_states[vkey] = 0;
        if (keyboard_state_.pressed_key_count > 0) {
            --keyboard_state_.pressed_key_count;
        }
    }
}

void VirtualKeyboardDevice::set_focus_window(std::uint32_t wid) {
    std::lock_guard<std::mutex> guard(lock_);
    keyboard_state_.focused_window_id = wid;
}

void VirtualKeyboardDevice::set_modifiers(std::uint32_t mods) {
    std::lock_guard<std::mutex> guard(lock_);
    keyboard_state_.active_modifiers = mods;
}

void VirtualKeyboardDevice::release_all_keys() {
    std::lock_guard<std::mutex> guard(lock_);
    for (int i = 0; i < 256; ++i) {
        keyboard_state_.key_states[i] = 0;
    }
    keyboard_state_.pressed_key_count = 0;
    keyboard_state_.active_modifiers = 0;
}

KeyboardStateData VirtualKeyboardDevice::get_state() const {
    std::lock_guard<std::mutex> guard(lock_);
    return keyboard_state_;
}

bool VirtualKeyboardDevice::is_key_down(std::uint8_t vkey) const {
    std::lock_guard<std::mutex> guard(lock_);
    return keyboard_state_.key_states[vkey] != 0;
}

} // namespace winios::devices
