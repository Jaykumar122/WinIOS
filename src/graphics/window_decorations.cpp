#include "graphics/window_decorations.h"

#include <algorithm>
#include <cstring>

namespace winios::graphics {

void WindowDecorations::fill_rect(
    std::uint8_t* dst,
    std::uint32_t dst_w,
    std::uint32_t dst_h,
    std::uint32_t dst_stride,
    const Rect& r,
    std::uint32_t color) {

    if (!dst || r.width == 0 || r.height == 0) return;

    std::uint32_t x0 = std::min(r.x, dst_w);
    std::uint32_t y0 = std::min(r.y, dst_h);
    std::uint32_t x1 = std::min(r.x + r.width, dst_w);
    std::uint32_t y1 = std::min(r.y + r.height, dst_h);

    if (x0 >= x1 || y0 >= y1) return;

    const std::uint8_t b = static_cast<std::uint8_t>(color & 0xFF);
    const std::uint8_t g = static_cast<std::uint8_t>((color >> 8) & 0xFF);
    const std::uint8_t r_col = static_cast<std::uint8_t>((color >> 16) & 0xFF);
    const std::uint8_t a = static_cast<std::uint8_t>((color >> 24) & 0xFF);

    for (std::uint32_t y = y0; y < y1; ++y) {
        std::uint8_t* row = dst + (static_cast<std::size_t>(y) * dst_stride) + (static_cast<std::size_t>(x0) * 4);
        for (std::uint32_t x = x0; x < x1; ++x) {
            row[0] = b;
            row[1] = g;
            row[2] = r_col;
            row[3] = a;
            row += 4;
        }
    }
}

void WindowDecorations::render(
    std::uint8_t* dst_argb,
    std::uint32_t dst_width,
    std::uint32_t dst_height,
    std::uint32_t dst_stride,
    const Window& window) {

    if (!dst_argb || !window.is_visible()) return;

    const auto& f = window.frame;
    const bool is_active = window.is_focused;

    // 1. Draw outer border (1px)
    const std::uint32_t border_col = is_active ? kActiveAccentColor : kBorderColor;
    fill_rect(dst_argb, dst_width, dst_height, dst_stride, Rect{f.x, f.y, f.width, Window::kBorderWidth}, border_col); // Top
    fill_rect(dst_argb, dst_width, dst_height, dst_stride, Rect{f.x, f.y + f.height - Window::kBorderWidth, f.width, Window::kBorderWidth}, border_col); // Bottom
    fill_rect(dst_argb, dst_width, dst_height, dst_stride, Rect{f.x, f.y, Window::kBorderWidth, f.height}, border_col); // Left
    fill_rect(dst_argb, dst_width, dst_height, dst_stride, Rect{f.x + f.width - Window::kBorderWidth, f.y, Window::kBorderWidth, f.height}, border_col); // Right

    // 2. Draw Title Bar background
    const std::uint32_t title_bg = is_active ? kActiveTitleBarColor : kInactiveTitleBarColor;
    Rect title_rect{f.x + Window::kBorderWidth, f.y + Window::kBorderWidth,
                    (f.width > Window::kBorderWidth * 2) ? (f.width - Window::kBorderWidth * 2) : 0,
                    Window::kTitleBarHeight};
    fill_rect(dst_argb, dst_width, dst_height, dst_stride, title_rect, title_bg);

    // 3. Draw Active Accent Line beneath title bar if active
    if (is_active) {
        fill_rect(dst_argb, dst_width, dst_height, dst_stride,
                  Rect{title_rect.x, title_rect.y + title_rect.height - 1, title_rect.width, 1},
                  kActiveAccentColor);
    }

    // 4. Draw Title Bar Buttons (Close, Maximize, Minimize)
    if (f.width >= Window::kButtonWidth + Window::kButtonMargin + Window::kBorderWidth) {
        std::uint32_t close_x = f.x + f.width - Window::kBorderWidth - Window::kButtonMargin - Window::kButtonWidth;
        std::uint32_t btn_y = f.y + Window::kBorderWidth + (Window::kTitleBarHeight - Window::kButtonHeight) / 2;

        // Close button (red)
        fill_rect(dst_argb, dst_width, dst_height, dst_stride,
                  Rect{close_x, btn_y, Window::kButtonWidth, Window::kButtonHeight},
                  kCloseButtonColor);

        // Maximize button (green)
        if (f.width >= (Window::kButtonWidth * 2 + Window::kButtonMargin * 2 + Window::kBorderWidth)) {
            std::uint32_t max_x = close_x - Window::kButtonMargin - Window::kButtonWidth;
            fill_rect(dst_argb, dst_width, dst_height, dst_stride,
                      Rect{max_x, btn_y, Window::kButtonWidth, Window::kButtonHeight},
                      kMaximizeButtonColor);

            // Minimize button (yellow)
            if (f.width >= (Window::kButtonWidth * 3 + Window::kButtonMargin * 3 + Window::kBorderWidth)) {
                std::uint32_t min_x = max_x - Window::kButtonMargin - Window::kButtonWidth;
                fill_rect(dst_argb, dst_width, dst_height, dst_stride,
                          Rect{min_x, btn_y, Window::kButtonWidth, Window::kButtonHeight},
                          kMinimizeButtonColor);
            }
        }
    }
}

} // namespace winios::graphics
