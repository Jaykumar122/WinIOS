#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "device/apple_display.h"
#include "input/host_input_queue.h"
#include "input/input_provenance.h"
#include "input/virtual_input_device.h"

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
// Display Lifecycle State
// -----------------------------------------------------------------------------
enum class DisplayLifecycleState : std::uint32_t {
    Uninitialized = 0,
    Created       = 1,
    Visible       = 2,
    Hidden        = 3,
    Minimized     = 4,
    Closed        = 5
};

// -----------------------------------------------------------------------------
// Presentation Statistics
// -----------------------------------------------------------------------------
struct PresentationStats {
    std::uint64_t total_frames_presented{0};
    std::uint64_t total_frames_dropped{0};
    std::uint64_t last_presentation_time_ns{0};
    double average_frame_interval_ms{0.0};
    std::uint32_t client_width{0};
    std::uint32_t client_height{0};
    DisplayLifecycleState lifecycle_state{DisplayLifecycleState::Uninitialized};
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
    using KeyCallback = std::function<void(std::uint32_t key_code, bool is_down)>;
    using CharCallback = std::function<void(char32_t ch)>;
    using EventCallback = std::function<void(const input::InputEvent& event)>;

    explicit DisplayWindow(const DisplayWindowConfig& config = {});
    ~DisplayWindow();

    DisplayWindow(const DisplayWindow&) = delete;
    DisplayWindow& operator=(const DisplayWindow&) = delete;

    // Window Lifecycle
    bool create();
    void close();
    void show();
    void hide();
    void set_minimized(bool min);
    void handle_resize(std::uint32_t new_width, std::uint32_t new_height);
    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] bool is_headless() const noexcept { return config_.headless; }
    [[nodiscard]] DisplayLifecycleState lifecycle_state() const noexcept;
    [[nodiscard]] void* native_handle() const noexcept { return hwnd_; }
    [[nodiscard]] bool is_focused() const noexcept { return is_focused_; }
    [[nodiscard]] bool has_mouse_capture() const noexcept { return has_mouse_capture_; }
    void set_focused(bool focused);
    void set_mouse_capture(bool capture);

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
    [[nodiscard]] PresentationStats presentation_stats() const noexcept;

    // Touch & Input
    void set_touch_callback(TouchCallback cb) { touch_callback_ = std::move(cb); }
    void set_key_callback(KeyCallback cb) { key_callback_ = std::move(cb); }
    void set_char_callback(CharCallback cb) { char_callback_ = std::move(cb); }
    void set_event_callback(EventCallback cb) { event_callback_ = std::move(cb); }
    void set_input_queue(std::shared_ptr<input::HostInputQueue> q) { input_queue_ = std::move(q); }
    [[nodiscard]] std::shared_ptr<input::HostInputQueue> input_queue() const noexcept { return input_queue_; }

    [[nodiscard]] TouchEvent last_touch() const noexcept;
    void inject_touch(std::uint32_t x, std::uint32_t y, bool is_down);
    void inject_key(std::uint32_t key_code, bool is_down);
    void inject_char(char32_t ch);

    // Stage 216 Rich Input Injection APIs (with provenance)
    void inject_mouse_move(std::int32_t x, std::int32_t y, input::InputProvenance provenance = input::InputProvenance::TestInjected);
    void inject_mouse_button(input::MouseButton btn, bool is_down, std::int32_t x, std::int32_t y, input::InputProvenance provenance = input::InputProvenance::TestInjected);
    void inject_mouse_wheel(std::int32_t delta, std::int32_t x, std::int32_t y, input::InputProvenance provenance = input::InputProvenance::TestInjected);
    void inject_key_event(std::uint32_t key_code, bool is_down, bool is_repeat, std::uint32_t modifiers, input::InputProvenance provenance = input::InputProvenance::TestInjected);
    void inject_char_event(char32_t ch, input::InputProvenance provenance = input::InputProvenance::TestInjected);

    // Win32 Message Handlers
    void on_win32_mouse_move(int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected = false);
    void on_win32_mouse_button(input::MouseButton btn, bool is_down, int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected = false);
    void on_win32_mouse_wheel(int delta, int cx, int cy, int sx, int sy, std::uint64_t wparam, bool is_injected = false);
    void on_win32_mouse_leave();
    void on_win32_key(std::uint64_t wparam, std::uint64_t lparam, bool is_injected = false);
    void on_win32_char(std::uint64_t wparam, std::uint64_t lparam, bool is_injected = false);

    // Screenshot Capture
    [[nodiscard]] bool save_screenshot_bmp(const std::string& path) const;
    [[nodiscard]] bool capture_framebuffer_bmp(const std::string& path) const { return save_screenshot_bmp(path); }
    [[nodiscard]] bool capture_client_area_bmp(const std::string& path) const;
    [[nodiscard]] const std::vector<std::uint8_t>& backbuffer() const noexcept { return backbuffer_; }

    void blit_to_dc(void* hdc);

private:
    void update_window_geometry();

    DisplayWindowConfig config_;
    bool is_open_{false};
    DisplayLifecycleState lifecycle_state_{DisplayLifecycleState::Uninitialized};
    std::uint32_t window_width_{412};
    std::uint32_t window_height_{894};
    std::uint64_t total_frames_{0};
    std::uint64_t total_dropped_frames_{0};
    std::uint64_t last_presentation_time_ns_{0};
    double average_frame_interval_ms_{0.0};

    mutable std::mutex lock_;
    std::vector<std::uint8_t> backbuffer_;
    TouchEvent last_touch_{};
    TouchCallback touch_callback_;
    KeyCallback key_callback_;
    CharCallback char_callback_;
    EventCallback event_callback_;
    std::shared_ptr<input::HostInputQueue> input_queue_;
    bool is_focused_{false};
    bool has_mouse_capture_{false};

    // Windows native handles (stored as opaque pointers to avoid leaking Windows.h)
    void* hwnd_{nullptr};
    void* hdc_{nullptr};
    void* hbitmap_{nullptr};
    void* dib_pixels_{nullptr};
};

} // namespace winios::ui
