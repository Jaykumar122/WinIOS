#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Compositor Statistics & Diagnostics
// -----------------------------------------------------------------------------
struct CompositorStats {
    std::uint64_t total_frames{0};
    std::uint64_t full_redraw_frames{0};
    std::uint64_t partial_redraw_frames{0};
    std::uint64_t skipped_frames{0};
    std::uint64_t total_pixels_composited{0};
};

// -----------------------------------------------------------------------------
// Compositor: Deterministic Multi-Surface Z-Ordered Compositing Engine
// -----------------------------------------------------------------------------
class Compositor {
public:
    explicit Compositor(std::shared_ptr<VirtualDisplay> display);
    ~Compositor() = default;

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    [[nodiscard]] std::shared_ptr<VirtualDisplay> display() const noexcept { return display_; }
    [[nodiscard]] const CompositorStats& stats() const noexcept { return stats_; }

    // Surface registration
    bool add_surface(std::shared_ptr<DisplaySurface> surface);
    bool remove_surface(std::uint32_t surface_id);
    void clear_surfaces();

    // Damage & Invalidation
    void invalidate_full_frame() noexcept;
    void invalidate_rect(const Rect& rect) noexcept;

    // Background color (default black ARGB)
    void set_background_color(std::uint32_t color) noexcept { background_color_ = color; }
    [[nodiscard]] std::uint32_t background_color() const noexcept { return background_color_; }

    // Compositing passes
    // force_full = true ignores damage tracking and renders the entire screen
    bool composite(bool force_full = false);

private:
    std::shared_ptr<VirtualDisplay>              display_;
    std::uint32_t                                background_color_{0xFF000000u}; // Opaque black
    DamageTracker                                damage_;
    CompositorStats                              stats_{};
    mutable std::mutex                           lock_;
    std::vector<std::shared_ptr<DisplaySurface>> surfaces_;
};

} // namespace winios::graphics
