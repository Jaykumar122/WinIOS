#pragma once

#include "devices/virtual_device_interface.h"
#include "input/virtual_input_device.h"

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <array>

namespace winios::devices {

#pragma pack(push, 8)
struct PointerStateData {
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t buttons{0}; // Bit 0 = Left, Bit 1 = Right, Bit 2 = Middle
    std::int32_t wheel_delta{0};
    std::uint32_t modifiers{0};
    bool is_captured{false};
};

struct KeyboardStateData {
    std::uint32_t focused_window_id{0};
    std::uint32_t active_modifiers{0};
    std::uint32_t pressed_key_count{0};
    std::uint8_t  key_states[256]{0};
};
#pragma pack(pop)

class VirtualPointerDevice : public VirtualDeviceBase {
public:
    VirtualPointerDevice();
    ~VirtualPointerDevice() override = default;

    void update_position(std::int32_t x, std::int32_t y);
    void set_button(std::uint8_t button_index, bool down);
    void set_wheel(std::int32_t delta);
    void set_captured(bool captured);
    void release_all_buttons();

    [[nodiscard]] PointerStateData get_state() const;

private:
    mutable std::mutex lock_;
    PointerStateData pointer_state_{};
};

class VirtualKeyboardDevice : public VirtualDeviceBase {
public:
    VirtualKeyboardDevice();
    ~VirtualKeyboardDevice() override = default;

    void set_key(std::uint8_t vkey, bool down);
    void set_focus_window(std::uint32_t wid);
    void set_modifiers(std::uint32_t mods);
    void release_all_keys();

    [[nodiscard]] KeyboardStateData get_state() const;
    [[nodiscard]] bool is_key_down(std::uint8_t vkey) const;

private:
    mutable std::mutex lock_;
    KeyboardStateData keyboard_state_{};
};

} // namespace winios::devices
