#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "runtime/ui/ui_types.h"

namespace winios::runtime::ui {

enum class GestureType : std::uint8_t {
    Tap,
    DoubleTap,
    LongPress,
    Pan,
    Swipe,
    Cancel
};

enum class SwipeDirection : std::uint8_t {
    None,
    Left,
    Right,
    Up,
    Down
};

struct GestureEvent {
    GestureType type{GestureType::Tap};
    std::uint32_t contact_id{0};
    Point start_point{0, 0};
    Point current_point{0, 0};
    std::int32_t delta_x{0};
    std::int32_t delta_y{0};
    SwipeDirection swipe_dir{SwipeDirection::None};
    std::uint64_t duration_us{0};
};

class GestureRecognizer {
public:
    using GestureCallback = std::function<void(const GestureEvent&)>;

    struct Config {
        std::uint64_t tap_timeout_us{300000};       // 300 ms
        std::uint64_t double_tap_timeout_us{350000};// 350 ms
        std::uint64_t long_press_timeout_us{500000};// 500 ms
        std::int32_t tap_slop_px{10};               // Max movement for a tap
        std::int32_t pan_threshold_px{10};          // Movement to trigger pan
        std::int32_t swipe_threshold_px{40};        // Movement to trigger swipe
    };

    explicit GestureRecognizer(const Config& config = {});

    void on_touch_begin(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    void on_touch_move(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    void on_touch_end(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    void on_touch_cancel(std::uint32_t contact_id, std::uint64_t timestamp_us);

    void set_gesture_callback(GestureCallback cb) { callback_ = std::move(cb); }

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    struct ContactState {
        Point start{0, 0};
        Point current{0, 0};
        std::uint64_t start_time_us{0};
        bool is_panning{false};
        bool is_long_press_fired{false};
    };

    Config config_;
    GestureCallback callback_;
    std::unordered_map<std::uint32_t, ContactState> contacts_;

    // Double tap state
    Point last_tap_point_{0, 0};
    std::uint64_t last_tap_time_us_{0};
};

} // namespace winios::runtime::ui
