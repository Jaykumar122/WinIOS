#pragma once

#include <cstdint>
#include <vector>
#include "runtime/ui/ui_types.h"

namespace winios::runtime::ui {

class RenderContext {
public:
    RenderContext(std::uint8_t* buffer, std::uint32_t width, std::uint32_t height, std::uint32_t stride);

    [[nodiscard]] std::uint8_t* buffer() const noexcept { return buffer_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t stride() const noexcept { return stride_; }

    // Clip rect management
    void push_clip(const Rect& clip);
    void pop_clip();
    [[nodiscard]] Rect current_clip() const noexcept;

    // Invalidation / dirty tracking
    void invalidate(const Rect& dirty);
    [[nodiscard]] bool has_damage() const noexcept { return has_damage_; }
    [[nodiscard]] Rect dirty_bounds() const noexcept { return dirty_bounds_; }
    void clear_damage() noexcept;

    // Primitives
    void fill_rect(const Rect& r, std::uint32_t color_bgra);
    void draw_rect_outline(const Rect& r, std::uint32_t color_bgra, std::int32_t thickness = 1);
    void draw_rounded_rect(const Rect& r, std::int32_t radius, std::uint32_t color_bgra);
    void draw_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t color_bgra);
    void draw_char(std::int32_t x, std::int32_t y, char c, std::uint32_t color_bgra, std::uint32_t scale = 1);
    void draw_string(std::int32_t x, std::int32_t y, const char* text, std::uint32_t color_bgra, std::uint32_t scale = 1);

private:
    void set_pixel_clipped(std::int32_t x, std::int32_t y, std::uint32_t color_bgra);

    std::uint8_t* buffer_{nullptr};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::uint32_t stride_{0};

    std::vector<Rect> clip_stack_;
    Rect dirty_bounds_{0, 0, 0, 0};
    bool has_damage_{false};
};

} // namespace winios::runtime::ui
