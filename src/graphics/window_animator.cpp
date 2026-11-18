#include "graphics/window_animator.h"

#include <algorithm>

namespace winios::graphics {

void WindowAnimator::start_animation(
    std::uint32_t window_id,
    WindowAnimationType type,
    const Rect& start_frame,
    const Rect& target_frame,
    std::uint32_t duration_ms) {

    std::lock_guard<std::mutex> guard(lock_);
    WindowAnimationState st{};
    st.window_id = window_id;
    st.type = type;
    st.duration_ms = (duration_ms == 0) ? 1 : duration_ms;
    st.elapsed_ms = 0;
    st.is_finished = false;
    st.start_frame = start_frame;
    st.target_frame = target_frame;

    if (type == WindowAnimationType::Open) {
        st.current_scale = 0.8f;
        st.current_alpha = 0.0f;
    } else if (type == WindowAnimationType::Close) {
        st.current_scale = 1.0f;
        st.current_alpha = 1.0f;
    } else if (type == WindowAnimationType::Minimize) {
        st.current_scale = 1.0f;
        st.current_alpha = 1.0f;
    } else if (type == WindowAnimationType::Restore) {
        st.current_scale = 0.5f;
        st.current_alpha = 0.2f;
    } else {
        st.current_scale = 1.0f;
        st.current_alpha = 1.0f;
    }

    animations_[window_id] = st;
}

void WindowAnimator::step(std::uint32_t delta_ms) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, st] : animations_) {
        if (st.is_finished) continue;

        st.elapsed_ms += delta_ms;
        float progress = static_cast<float>(st.elapsed_ms) / static_cast<float>(st.duration_ms);
        if (progress >= 1.0f) {
            progress = 1.0f;
            st.is_finished = true;
        }

        // Smooth ease-out quad interpolation
        float t = 1.0f - (1.0f - progress) * (1.0f - progress);

        switch (st.type) {
        case WindowAnimationType::Open:
            st.current_scale = 0.8f + 0.2f * t;
            st.current_alpha = t;
            break;
        case WindowAnimationType::Close:
            st.current_scale = 1.0f - 0.2f * t;
            st.current_alpha = 1.0f - t;
            break;
        case WindowAnimationType::Minimize:
            st.current_scale = 1.0f - 0.5f * t;
            st.current_alpha = 1.0f - 0.8f * t;
            break;
        case WindowAnimationType::Restore:
            st.current_scale = 0.5f + 0.5f * t;
            st.current_alpha = 0.2f + 0.8f * t;
            break;
        default:
            st.current_scale = 1.0f;
            st.current_alpha = 1.0f;
            break;
        }
    }
}

bool WindowAnimator::is_animating(std::uint32_t window_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = animations_.find(window_id);
    return it != animations_.end() && !it->second.is_finished;
}

bool WindowAnimator::has_active_animations() const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& [_, st] : animations_) {
        if (!st.is_finished) return true;
    }
    return false;
}

Rect WindowAnimator::get_current_frame(std::uint32_t window_id, const Rect& fallback) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = animations_.find(window_id);
    if (it == animations_.end() || it->second.is_finished) {
        return fallback;
    }

    const auto& st = it->second;
    float t = static_cast<float>(st.elapsed_ms) / static_cast<float>(st.duration_ms);
    t = std::clamp(t, 0.0f, 1.0f);
    float ease = 1.0f - (1.0f - t) * (1.0f - t);

    float cx = static_cast<float>(st.start_frame.x) + (static_cast<float>(st.target_frame.x) - static_cast<float>(st.start_frame.x)) * ease;
    float cy = static_cast<float>(st.start_frame.y) + (static_cast<float>(st.target_frame.y) - static_cast<float>(st.start_frame.y)) * ease;
    float cw = static_cast<float>(st.start_frame.width) + (static_cast<float>(st.target_frame.width) - static_cast<float>(st.start_frame.width)) * ease;
    float ch = static_cast<float>(st.start_frame.height) + (static_cast<float>(st.target_frame.height) - static_cast<float>(st.start_frame.height)) * ease;

    return Rect{
        static_cast<std::uint32_t>(std::max(0.0f, cx)),
        static_cast<std::uint32_t>(std::max(0.0f, cy)),
        static_cast<std::uint32_t>(std::max(1.0f, cw)),
        static_cast<std::uint32_t>(std::max(1.0f, ch))
    };
}

float WindowAnimator::get_current_alpha(std::uint32_t window_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = animations_.find(window_id);
    if (it == animations_.end() || it->second.is_finished) {
        return 1.0f;
    }
    return it->second.current_alpha;
}

void WindowAnimator::cancel_animation(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    animations_.erase(window_id);
}

void WindowAnimator::clear() {
    std::lock_guard<std::mutex> guard(lock_);
    animations_.clear();
}

} // namespace winios::graphics
