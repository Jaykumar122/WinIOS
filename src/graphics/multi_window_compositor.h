#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "graphics/window_decorations.h"

namespace winios::graphics {

struct MultiWindowCompositorStats {
    std::uint64_t total_frames{0};
    std::uint64_t full_redraw_frames{0};
    std::uint64_t partial_redraw_frames{0};
    std::uint64_t skipped_frames{0};
    std::uint64_t total_pixels_composited{0};
    std::uint64_t occluded_windows_skipped{0};
};

class MultiWindowCompositor {
public:
    explicit MultiWindowCompositor(
        std::shared_ptr<WindowManager> window_mgr,
        std::shared_ptr<VirtualDisplay> display);
    ~MultiWindowCompositor() = default;

    MultiWindowCompositor(const MultiWindowCompositor&) = delete;
    MultiWindowCompositor& operator=(const MultiWindowCompositor&) = delete;

    [[nodiscard]] std::shared_ptr<WindowManager> window_manager() const noexcept { return window_mgr_; }
    [[nodiscard]] std::shared_ptr<VirtualDisplay> display() const noexcept { return display_; }
    [[nodiscard]] const MultiWindowCompositorStats& stats() const noexcept { return stats_; }

    void set_background_color(std::uint32_t color) noexcept { background_color_ = color; }
    [[nodiscard]] std::uint32_t background_color() const noexcept { return background_color_; }

    void invalidate_full_desktop() noexcept;
    void invalidate_rect(const Rect& rect) noexcept;

    // Perform desktop composition
    bool composite(bool force_full = false);

private:
    std::shared_ptr<WindowManager>      window_mgr_;
    std::shared_ptr<VirtualDisplay>     display_;
    std::uint32_t                       background_color_{0xFF0B0E14}; // Deep slate desktop
    DamageTracker                       desktop_damage_;
    MultiWindowCompositorStats          stats_{};
    mutable std::mutex                  lock_;

    void clear_damaged_background(const Rect& damage);
    void blit_client_surface(const Window& window, const Rect& damage);
};

} // namespace winios::graphics
