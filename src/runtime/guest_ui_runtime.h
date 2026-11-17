#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "graphics/virtual_display.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// GuestInputEvent: 32-byte struct binary-compatible with guest syscall 504
// -----------------------------------------------------------------------------
struct GuestInputEvent {
    std::uint32_t type{0};         // 1=TouchBegin, 2=TouchMove, 3=TouchEnd, 4=TouchCancel, 5=Key, 7=Home, 8=Lock
    std::uint32_t contact_id{0};   // 0..9
    std::int32_t  x{0};            // Touch coordinate
    std::int32_t  y{0};            // Touch coordinate
    std::uint32_t key_code{0};     // Key or button code
    std::uint32_t window_id{0};    // Window receiving the event
    std::uint64_t timestamp_us{0}; // Microseconds
};

// -----------------------------------------------------------------------------
// GuestUiRuntime: Text, Typography, Buttons & Surface Drawing Helpers
// -----------------------------------------------------------------------------
class GuestUiRuntime {
public:
    // Render single 8x8 ASCII character with scaling
    static void draw_char_8x8(
        std::uint8_t* buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride,
        graphics::PixelFormat fmt,
        std::int32_t x,
        std::int32_t y,
        char c,
        std::uint32_t color,
        std::uint32_t scale = 1) noexcept;

    // Render null-terminated ASCII string with scaling and character spacing
    static void draw_string_8x8(
        std::uint8_t* buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride,
        graphics::PixelFormat fmt,
        std::int32_t x,
        std::int32_t y,
        const char* text,
        std::uint32_t color,
        std::uint32_t scale = 1) noexcept;

    // Render interactive button with 3D border and centered text
    static void draw_button(
        std::uint8_t* buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride,
        graphics::PixelFormat fmt,
        const graphics::Rect& frame,
        const char* label,
        std::uint32_t bg_color,
        std::uint32_t text_color,
        bool is_pressed = false) noexcept;

    // Render iOS-like home screen application icon with rounded appearance
    static void draw_app_icon(
        std::uint8_t* buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride,
        graphics::PixelFormat fmt,
        const graphics::Rect& frame,
        const char* label,
        std::uint32_t icon_color) noexcept;

    // Render status bar at top of portrait display (clock, wifi, battery)
    static void draw_status_bar(
        std::uint8_t* buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride,
        graphics::PixelFormat fmt,
        const char* time_str = "9:41",
        std::uint32_t battery_pct = 100) noexcept;
};

} // namespace winios::runtime
