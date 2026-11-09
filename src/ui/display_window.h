#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "device/apple_display.h"

namespace winios::ui {

// -----------------------------------------------------------------------------
// DisplayWindowConfig: Configuration for the iPhone 15 presentation surface
// -----------------------------------------------------------------------------
struct DisplayWindowConfig {
    std::string title{"WinIOS - iPhone 15 (iPhone15,4 / Apple A16 Bionic)"};
    std::uint32_t native_width{device::AppleDisplay::kDefaultWidth};   // 1179
    std::uint32_t native_height{device::AppleDisplay::kDefaultHeight}; // 2556
    float scale{0.35f}; // Default ~412 x 894 desktop window footprint
    bool headless{false}; // Offscreen mode for automated CI / non-GUI testing
    bool preserve_aspect_ratio{true};
};

// -----------------------------------------------------------------------------
// TouchEvent: Input coordinates mapped to native iPhone 15 panel
// -----------------------------------------------------------------------------
struct TouchEvent {
    std::uint32_t x{0}; // 0 .. native_width - 1
    std::uint32_t y{0}; // 0 .. native_height - 1
    bool is_down{false};
};

// -----------------------------------------------------------------------------
// DisplayWindow: Native Windows GUI Surface for iPhone 15 Framebuffer
// -----------------------------------------------------------------------------
class DisplayWindow {
public:
    using TouchCallback = std::function<void(const TouchEvent& event)>;

    explicit DisplayWindow(const DisplayWindowConfig& config = {});
    ~DisplayWindow();

    DisplayWindow(const DisplayWindow&) = delete;
    DisplayWindow& operator=(const DisplayWindow&) = delete;

    // Window Lifecycle
    bool create();
    void close();
    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] bool is_headless() const noexcept { return config_.headless; }

    // Message Loop Processing
    bool pump_events();

    // Frame Presentation
    bool present_frame(const device::AppleDisplay& display);
    bool present_frame(const std::uint8_t* raw_argb, std::uint32_t width, std::uint32_t height);

    // Diagnostics & Statistics
    [[nodiscard]] std::uint64_t total_frames_presented() const noexcept { return total_frames_; }
    [[nodiscard]] std::uint32_t window_width() const noexcept { return window_width_; }
    [[nodiscard]] std::uint32_t window_height() const noexcept { return window_height_; }
    [[nodiscard]] const DisplayWindowConfig& config() const noexcept { return config_; }

    // Touch & Input
    void set_touch_callback(TouchCallback cb) { touch_callback_ = std::move(cb); }
    [[nodiscard]] TouchEvent last_touch() const noexcept;
    void inject_touch(std::uint32_t x, std::uint32_t y, bool is_down);

    // Screenshot Capture
    [[nodiscard]] bool save_screenshot_bmp(const std::string& path) const;

private:
    void update_window_geometry();
    void blit_to_dc(void* hdc);

    DisplayWindowConfig config_;
    bool is_open_{false};
    std::uint32_t window_width_{412};
    std::uint32_t window_height_{894};
    std::uint64_t total_frames_{0};

    mutable std::mutex lock_;
    std::vector<std::uint8_t> backbuffer_;
    TouchEvent last_touch_{};
    TouchCallback touch_callback_;

    // Windows native handles (stored as opaque pointers to avoid leaking Windows.h)
    void* hwnd_{nullptr};
    void* hdc_{nullptr};
    void* hbitmap_{nullptr};
    void* dib_pixels_{nullptr};
};

} // namespace winios::ui
