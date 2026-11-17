#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "input/virtual_input_device.h"

namespace winios::input {

enum class GestureType : std::uint32_t {
    None = 0,
    Tap = 1,
    DoubleTap = 2,
    LongPress = 3,
    Pan = 4,
    Pinch = 5
};

struct GestureEvent {
    GestureType   type{GestureType::None};
    std::int32_t  x{0};
    std::int32_t  y{0};
    std::int32_t  delta_x{0};
    std::int32_t  delta_y{0};
    float         scale{1.0f};
    std::uint64_t timestamp_us{0};
};

class GestureRecognizer {
public:
    using GestureCallback = std::function<void(const GestureEvent& evt)>;

    GestureRecognizer();
    ~GestureRecognizer() = default;

    void set_gesture_callback(GestureCallback cb) { callback_ = std::move(cb); }
    void process_input_event(const InputEvent& event);
    void reset();

private:
    GestureCallback callback_;

    bool touch_active_{false};
    std::int32_t start_x_{0};
    std::int32_t start_y_{0};
    std::int32_t last_x_{0};
    std::int32_t last_y_{0};
    std::uint64_t start_time_us_{0};
    std::uint64_t last_tap_time_us_{0};
    std::int32_t last_tap_x_{0};
    std::int32_t last_tap_y_{0};
    bool is_panning_{false};
    bool long_press_fired_{false};
};

class FocusNavigator {
public:
    explicit FocusNavigator(std::vector<std::uint32_t> control_ids = {});
    ~FocusNavigator() = default;

    void set_controls(std::vector<std::uint32_t> control_ids);
    [[nodiscard]] std::uint32_t current_focus() const noexcept;
    void set_focus(std::uint32_t control_id);

    // Navigate to next control (Tab)
    std::uint32_t focus_next();

    // Navigate to previous control (Shift+Tab)
    std::uint32_t focus_previous();

private:
    std::vector<std::uint32_t> controls_;
    std::size_t current_index_{0};
};

} // namespace winios::input
