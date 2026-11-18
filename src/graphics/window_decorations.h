#pragma once

#include <cstdint>
#include <string>

#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"

namespace winios::graphics {

class WindowDecorations {
public:
    static constexpr std::uint32_t kActiveTitleBarColor   = 0xFF1E2430;
    static constexpr std::uint32_t kInactiveTitleBarColor = 0xFF2A2D34;
    static constexpr std::uint32_t kActiveTitleTextColor  = 0xFFFFFFFF;
    static constexpr std::uint32_t kInactiveTitleTextColor= 0xFF8E8E93;
    static constexpr std::uint32_t kBorderColor           = 0xFF3A3D45;
    static constexpr std::uint32_t kActiveAccentColor     = 0xFF007AFF;
    static constexpr std::uint32_t kCloseButtonColor      = 0xFFFF3B30;
    static constexpr std::uint32_t kMinimizeButtonColor   = 0xFFFFCC00;
    static constexpr std::uint32_t kMaximizeButtonColor   = 0xFF34C759;

    static void render(
        std::uint8_t* dst_argb,
        std::uint32_t dst_width,
        std::uint32_t dst_height,
        std::uint32_t dst_stride,
        const Window& window);

private:
    static void fill_rect(
        std::uint8_t* dst,
        std::uint32_t dst_w,
        std::uint32_t dst_h,
        std::uint32_t dst_stride,
        const Rect& r,
        std::uint32_t color);
};

} // namespace winios::graphics
