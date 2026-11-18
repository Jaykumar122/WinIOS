#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include "graphics/virtual_display.h"

namespace winios::graphics {

enum class WindowAnimationType : std::uint8_t {
    None     = 0,
    Open     = 1,
    Close    = 2,
    Minimize = 3,
    Restore  = 4
};

struct WindowAnimationState {
    std::uint32_t       window_id{0};
    WindowAnimationType type{WindowAnimationType::None};
    std::uint32_t       duration_ms{250};
    std::uint32_t       elapsed_ms{0};
    bool                is_finished{false};
    float               current_scale{1.0f};
    float               current_alpha{1.0f};
    Rect                start_frame{0, 0, 0, 0};
    Rect                target_frame{0, 0, 0, 0};
};

class WindowAnimator {
public:
    WindowAnimator() = default;
    ~WindowAnimator() = default;

    void start_animation(
        std::uint32_t window_id,
        WindowAnimationType type,
        const Rect& start_frame,
        const Rect& target_frame,
        std::uint32_t duration_ms = 250);

    void step(std::uint32_t delta_ms);

    [[nodiscard]] bool is_animating(std::uint32_t window_id) const;
    [[nodiscard]] bool has_active_animations() const;

    [[nodiscard]] Rect get_current_frame(std::uint32_t window_id, const Rect& fallback) const;
    [[nodiscard]] float get_current_alpha(std::uint32_t window_id) const;

    void cancel_animation(std::uint32_t window_id);
    void clear();

private:
    mutable std::mutex lock_;
    std::map<std::uint32_t, WindowAnimationState> animations_;
};

} // namespace winios::graphics
