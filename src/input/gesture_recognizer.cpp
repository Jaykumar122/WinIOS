#include "input/gesture_recognizer.h"

#include <cmath>
#include <algorithm>

namespace winios::input {

GestureRecognizer::GestureRecognizer() = default;

void GestureRecognizer::reset() {
    touch_active_ = false;
    start_x_ = 0;
    start_y_ = 0;
    last_x_ = 0;
    last_y_ = 0;
    start_time_us_ = 0;
    is_panning_ = false;
    long_press_fired_ = false;
}

void GestureRecognizer::process_input_event(const InputEvent& event) {
    if (event.type == InputEventType::TouchBegin) {
        touch_active_ = true;
        start_x_ = event.touch.x;
        start_y_ = event.touch.y;
        last_x_ = start_x_;
        last_y_ = start_y_;
        start_time_us_ = event.timestamp_us;
        is_panning_ = false;
        long_press_fired_ = false;
    } else if (event.type == InputEventType::TouchMove && touch_active_) {
        const std::int32_t dx = event.touch.x - start_x_;
        const std::int32_t dy = event.touch.y - start_y_;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));

        if (dist > 10.0) {
            is_panning_ = true;
            if (callback_) {
                GestureEvent ge{};
                ge.type = GestureType::Pan;
                ge.x = event.touch.x;
                ge.y = event.touch.y;
                ge.delta_x = event.touch.x - last_x_;
                ge.delta_y = event.touch.y - last_y_;
                ge.timestamp_us = event.timestamp_us;
                callback_(ge);
            }
        } else if (!is_panning_ && !long_press_fired_) {
            // Check long press duration (e.g. 600ms = 600,000 us)
            if (event.timestamp_us >= start_time_us_ + 600000ULL) {
                long_press_fired_ = true;
                if (callback_) {
                    GestureEvent ge{};
                    ge.type = GestureType::LongPress;
                    ge.x = event.touch.x;
                    ge.y = event.touch.y;
                    ge.timestamp_us = event.timestamp_us;
                    callback_(ge);
                }
            }
        }
        last_x_ = event.touch.x;
        last_y_ = event.touch.y;
    } else if (event.type == InputEventType::TouchEnd && touch_active_) {
        const std::int32_t dx = event.touch.x - start_x_;
        const std::int32_t dy = event.touch.y - start_y_;
        const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        const std::uint64_t duration_us = event.timestamp_us - start_time_us_;

        if (!is_panning_ && !long_press_fired_ && dist <= 15.0 && duration_us <= 500000ULL) {
            // Check double tap
            const std::int32_t dtx = event.touch.x - last_tap_x_;
            const std::int32_t dty = event.touch.y - last_tap_y_;
            const double tap_dist = std::sqrt(static_cast<double>(dtx * dtx + dty * dty));
            const std::uint64_t tap_interval_us = event.timestamp_us - last_tap_time_us_;

            if (tap_dist <= 20.0 && tap_interval_us <= 350000ULL && last_tap_time_us_ > 0) {
                if (callback_) {
                    GestureEvent ge{};
                    ge.type = GestureType::DoubleTap;
                    ge.x = event.touch.x;
                    ge.y = event.touch.y;
                    ge.timestamp_us = event.timestamp_us;
                    callback_(ge);
                }
                last_tap_time_us_ = 0; // Reset after double tap
            } else {
                if (callback_) {
                    GestureEvent ge{};
                    ge.type = GestureType::Tap;
                    ge.x = event.touch.x;
                    ge.y = event.touch.y;
                    ge.timestamp_us = event.timestamp_us;
                    callback_(ge);
                }
                last_tap_time_us_ = event.timestamp_us;
                last_tap_x_ = event.touch.x;
                last_tap_y_ = event.touch.y;
            }
        }
        touch_active_ = false;
        is_panning_ = false;
    } else if (event.type == InputEventType::TouchCancel) {
        touch_active_ = false;
        is_panning_ = false;
        long_press_fired_ = false;
    }
}

FocusNavigator::FocusNavigator(std::vector<std::uint32_t> control_ids)
    : controls_(std::move(control_ids)) {}

void FocusNavigator::set_controls(std::vector<std::uint32_t> control_ids) {
    controls_ = std::move(control_ids);
    current_index_ = 0;
}

std::uint32_t FocusNavigator::current_focus() const noexcept {
    if (controls_.empty()) return 0;
    return controls_[current_index_];
}

void FocusNavigator::set_focus(std::uint32_t control_id) {
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        if (controls_[i] == control_id) {
            current_index_ = i;
            return;
        }
    }
}

std::uint32_t FocusNavigator::focus_next() {
    if (controls_.empty()) return 0;
    current_index_ = (current_index_ + 1) % controls_.size();
    return controls_[current_index_];
}

std::uint32_t FocusNavigator::focus_previous() {
    if (controls_.empty()) return 0;
    current_index_ = (current_index_ + controls_.size() - 1) % controls_.size();
    return controls_[current_index_];
}

} // namespace winios::input
