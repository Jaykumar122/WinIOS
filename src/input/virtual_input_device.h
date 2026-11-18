#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "input/input_provenance.h"

namespace winios::input {

// -----------------------------------------------------------------------------
// Input Event Types
// -----------------------------------------------------------------------------
enum class InputEventType : std::uint32_t {
    TouchBegin  = 1,
    TouchMove   = 2,
    TouchEnd    = 3,
    TouchCancel = 4,
    KeyPress    = 5,
    KeyRelease  = 6,
    ButtonHome  = 7,
    ButtonLock  = 8,
    MouseMove   = 9,
    MouseDown   = 10,
    MouseUp     = 11,
    MouseWheel  = 12,
    CharInput   = 13,
    WindowFocus = 14,
    WindowBlur  = 15,
    TimerTick   = 16
};

enum class MouseButton : std::uint8_t {
    None   = 0,
    Left   = 1,
    Right  = 2,
    Middle = 3
};

struct MouseData {
    std::int32_t x{0};          // Virtual display coordinates
    std::int32_t y{0};
    std::int32_t client_x{0};
    std::int32_t client_y{0};
    std::int32_t screen_x{0};
    std::int32_t screen_y{0};
    MouseButton  button{MouseButton::None};
    std::int32_t wheel_delta{0}; // Positive = forward / up, Negative = backward / down
    std::uint32_t modifier_flags{0};
};

struct KeyboardData {
    std::uint32_t virtual_key{0};
    std::uint32_t vkey{0};
    std::uint32_t scan_code{0};
    char32_t      char_code{0};
    bool          is_down{false};
    bool          is_repeat{false};
    std::uint32_t modifiers{0};
    std::uint32_t modifier_flags{0};
};

// -----------------------------------------------------------------------------
// Touch Point Structure (Multi-touch Contact)
// -----------------------------------------------------------------------------
struct TouchPoint {
    std::uint32_t contact_id{0}; // Contact slot (0..9)
    std::int32_t  x{0};          // Virtual display coordinates
    std::int32_t  y{0};
    float         pressure{1.0f};
    bool          is_active{false};
};

// -----------------------------------------------------------------------------
// Input Event Structure (Delivered to Guest or Router)
// -----------------------------------------------------------------------------
struct InputEvent {
    InputEventType  type{InputEventType::TouchBegin};
    std::uint32_t   device_id{1};
    std::uint64_t   timestamp_us{0};
    TouchPoint      touch{};
    std::uint32_t   key_code{0};
    std::uint32_t   target_pid{0};       // Filled by InputRouter
    std::uint32_t   target_window_id{0}; // Filled by InputRouter
    InputProvenance provenance{InputProvenance::PhysicalWindowsInput};
    std::uint32_t   win32_msg_id{0};
    MouseData       mouse{};
    KeyboardData    keyboard{};
};

// -----------------------------------------------------------------------------
// VirtualInputDevice: Multi-Touch Digitizer & Hardware Buttons
// -----------------------------------------------------------------------------
class VirtualInputDevice {
public:
    static constexpr std::uint32_t kMaxTouchContacts = 10u;
    using EventDispatchCallback = std::function<void(const InputEvent& event)>;

    explicit VirtualInputDevice(std::uint32_t device_id = 1u);
    ~VirtualInputDevice() = default;

    // Reset state
    void reset();

    // Event callback
    void set_dispatch_callback(EventDispatchCallback cb) { dispatch_cb_ = std::move(cb); }

    // Touch injection
    bool inject_touch_begin(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    bool inject_touch_move(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    bool inject_touch_end(std::uint32_t contact_id, std::uint64_t timestamp_us);
    bool inject_touch_cancel(std::uint32_t contact_id, std::uint64_t timestamp_us);

    // Development keyboard / button injection
    bool inject_key(std::uint32_t key_code, bool is_down, std::uint64_t timestamp_us);
    bool inject_home_button(std::uint64_t timestamp_us);
    bool inject_lock_button(std::uint64_t timestamp_us);

    // Contact queries
    [[nodiscard]] bool is_contact_active(std::uint32_t contact_id) const noexcept;
    [[nodiscard]] TouchPoint get_contact(std::uint32_t contact_id) const noexcept;
    [[nodiscard]] std::uint32_t active_contact_count() const noexcept;

private:
    std::uint32_t         device_id_{1};
    mutable std::mutex    lock_;
    TouchPoint            contacts_[kMaxTouchContacts]{};
    EventDispatchCallback dispatch_cb_;
};

} // namespace winios::input
