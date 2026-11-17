#include "runtime/ui/gesture_recognizer.h"
#include <cmath>

namespace winios::runtime::ui {

GestureRecognizer::GestureRecognizer(const Config& config)
    : config_(config) {}

void GestureRecognizer::on_touch_begin(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us) {
    ContactState state{};
    state.start = {x, y};
    state.current = {x, y};
    state.start_time_us = timestamp_us;
    state.is_panning = false;
    state.is_long_press_fired = false;
    contacts_[contact_id] = state;
}

void GestureRecognizer::on_touch_move(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us) {
    auto it = contacts_.find(contact_id);
    if (it == contacts_.end()) return;

    auto& state = it->second;
    std::int32_t dx = x - state.start.x;
    std::int32_t dy = y - state.start.y;
    std::int32_t dist_sq = dx * dx + dy * dy;

    state.current = {x, y};

    // Check Pan
    if (!state.is_panning && dist_sq >= config_.pan_threshold_px * config_.pan_threshold_px) {
        state.is_panning = true;
    }

    if (state.is_panning && callback_) {
        GestureEvent ev{};
        ev.type = GestureType::Pan;
        ev.contact_id = contact_id;
        ev.start_point = state.start;
        ev.current_point = state.current;
        ev.delta_x = dx;
        ev.delta_y = dy;
        ev.duration_us = (timestamp_us >= state.start_time_us) ? (timestamp_us - state.start_time_us) : 0;
        callback_(ev);
    }

    // Check Long Press (only if stayed within slop and time exceeded)
    if (!state.is_panning && !state.is_long_press_fired && dist_sq < config_.tap_slop_px * config_.tap_slop_px) {
        if (timestamp_us - state.start_time_us >= config_.long_press_timeout_us) {
            state.is_long_press_fired = true;
            if (callback_) {
                GestureEvent ev{};
                ev.type = GestureType::LongPress;
                ev.contact_id = contact_id;
                ev.start_point = state.start;
                ev.current_point = state.current;
                ev.duration_us = timestamp_us - state.start_time_us;
                callback_(ev);
            }
        }
    }
}

void GestureRecognizer::on_touch_end(std::uint32_t contact_id, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us) {
    auto it = contacts_.find(contact_id);
    if (it == contacts_.end()) return;

    auto state = it->second;
    contacts_.erase(it);

    std::uint64_t duration = (timestamp_us >= state.start_time_us) ? (timestamp_us - state.start_time_us) : 0;
    std::int32_t dx = x - state.start.x;
    std::int32_t dy = y - state.start.y;
    std::int32_t dist_sq = dx * dx + dy * dy;

    if (state.is_long_press_fired) {
        return; // Long press already dispatched
    }

    // Check Swipe
    if (std::abs(dx) >= config_.swipe_threshold_px || std::abs(dy) >= config_.swipe_threshold_px) {
        SwipeDirection dir = SwipeDirection::None;
        if (std::abs(dx) > std::abs(dy)) {
            dir = (dx > 0) ? SwipeDirection::Right : SwipeDirection::Left;
        } else {
            dir = (dy > 0) ? SwipeDirection::Down : SwipeDirection::Up;
        }
        if (callback_) {
            GestureEvent ev{};
            ev.type = GestureType::Swipe;
            ev.contact_id = contact_id;
            ev.start_point = state.start;
            ev.current_point = {x, y};
            ev.delta_x = dx;
            ev.delta_y = dy;
            ev.swipe_dir = dir;
            ev.duration_us = duration;
            callback_(ev);
        }
        return;
    }

    // Check Tap / Double Tap
    if (dist_sq <= config_.tap_slop_px * config_.tap_slop_px && duration <= config_.tap_timeout_us) {
        // Check if double tap
        std::int32_t dt_dx = x - last_tap_point_.x;
        std::int32_t dt_dy = y - last_tap_point_.y;
        std::int32_t dt_dist_sq = dt_dx * dt_dx + dt_dy * dt_dy;
        std::uint64_t tap_interval = (timestamp_us >= last_tap_time_us_) ? (timestamp_us - last_tap_time_us_) : 1000000;

        if (tap_interval <= config_.double_tap_timeout_us && dt_dist_sq <= (config_.tap_slop_px * 2) * (config_.tap_slop_px * 2)) {
            // Double Tap!
            last_tap_time_us_ = 0;
            if (callback_) {
                GestureEvent ev{};
                ev.type = GestureType::DoubleTap;
                ev.contact_id = contact_id;
                ev.start_point = state.start;
                ev.current_point = {x, y};
                ev.duration_us = duration;
                callback_(ev);
            }
        } else {
            // Single Tap
            last_tap_point_ = {x, y};
            last_tap_time_us_ = timestamp_us;
            if (callback_) {
                GestureEvent ev{};
                ev.type = GestureType::Tap;
                ev.contact_id = contact_id;
                ev.start_point = state.start;
                ev.current_point = {x, y};
                ev.duration_us = duration;
                callback_(ev);
            }
        }
    }
}

void GestureRecognizer::on_touch_cancel(std::uint32_t contact_id, std::uint64_t timestamp_us) {
    auto it = contacts_.find(contact_id);
    if (it != contacts_.end()) {
        auto state = it->second;
        contacts_.erase(it);
        if (callback_) {
            GestureEvent ev{};
            ev.type = GestureType::Cancel;
            ev.contact_id = contact_id;
            ev.start_point = state.start;
            ev.current_point = state.current;
            ev.duration_us = (timestamp_us >= state.start_time_us) ? (timestamp_us - state.start_time_us) : 0;
            callback_(ev);
        }
    }
}

} // namespace winios::runtime::ui
