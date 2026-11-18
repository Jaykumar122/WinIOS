#pragma once

#include <memory>
#include <mutex>
#include <cstdint>
#include <vector>
#include <string>

#include "graphics/window_manager.h"
#include "graphics/multi_window_compositor.h"
#include "graphics/virtual_display.h"
#include "devices/display_service.h"
#include "devices/input_device_service.h"
#include "devices/notification_service.h"
#include "packaging/app_registry.h"
#include "kernel/app_supervisor.h"

namespace winios::graphics {

struct DesktopStats {
    std::uint64_t total_pointer_events{0};
    std::uint64_t total_keyboard_events{0};
    std::uint64_t total_drag_moves{0};
    std::uint64_t total_composited_frames{0};
    std::uint64_t notifications_rendered{0};
};

class DesktopController {
public:
    DesktopController(
        std::shared_ptr<WindowManager> window_mgr,
        std::shared_ptr<MultiWindowCompositor> compositor,
        std::shared_ptr<VirtualDisplay> display,
        std::shared_ptr<devices::VirtualDisplayService> disp_svc = nullptr,
        std::shared_ptr<devices::VirtualPointerDevice> ptr_dev = nullptr,
        std::shared_ptr<devices::VirtualKeyboardDevice> kbd_dev = nullptr,
        std::shared_ptr<devices::VirtualNotificationService> notif_svc = nullptr);

    ~DesktopController() = default;

    DesktopController(const DesktopController&) = delete;
    DesktopController& operator=(const DesktopController&) = delete;

    // Subsystem accessors
    [[nodiscard]] std::shared_ptr<WindowManager> window_manager() const noexcept { return window_mgr_; }
    [[nodiscard]] std::shared_ptr<MultiWindowCompositor> compositor() const noexcept { return compositor_; }
    [[nodiscard]] std::shared_ptr<VirtualDisplay> display() const noexcept { return display_; }
    [[nodiscard]] const DesktopStats& stats() const noexcept { return stats_; }

    // Pointer event ingest
    bool handle_pointer_move(std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    bool handle_pointer_button(std::uint32_t button, bool is_down, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);
    bool handle_pointer_wheel(std::int32_t delta, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us);

    // Keyboard event ingest
    bool handle_key_event(std::uint32_t vkey, bool is_down, std::uint32_t modifiers, std::uint64_t timestamp_us);
    bool handle_text_input(const std::string& utf8_text, std::uint64_t timestamp_us);

    // Desktop step / tick (Event delivery + composition)
    bool step(std::uint64_t delta_us = 16666);

    // Notification rendering
    void render_notification_overlay();

    // Display reconfiguration notification
    void on_display_reconfigured(std::uint32_t new_width, std::uint32_t new_height, float new_scale);

    // Active pointer capture state
    [[nodiscard]] bool is_dragging() const noexcept { return is_dragging_; }
    [[nodiscard]] std::uint64_t captured_window_handle() const noexcept { return captured_window_handle_; }
    void release_capture() noexcept;

private:
    std::shared_ptr<WindowManager>                       window_mgr_;
    std::shared_ptr<MultiWindowCompositor>              compositor_;
    std::shared_ptr<VirtualDisplay>                     display_;
    std::shared_ptr<devices::VirtualDisplayService>      disp_svc_;
    std::shared_ptr<devices::VirtualPointerDevice>      ptr_dev_;
    std::shared_ptr<devices::VirtualKeyboardDevice>     kbd_dev_;
    std::shared_ptr<devices::VirtualNotificationService> notif_svc_;

    mutable std::mutex lock_;
    DesktopStats       stats_{};

    // Pointer interaction & drag state
    bool          is_dragging_{false};
    std::uint64_t dragging_window_handle_{0};
    std::int32_t  drag_start_pointer_x_{0};
    std::int32_t  drag_start_pointer_y_{0};
    Rect          drag_start_window_frame_{0, 0, 0, 0};

    std::uint64_t captured_window_handle_{0};
    std::int32_t  last_pointer_x_{0};
    std::int32_t  last_pointer_y_{0};
    std::uint32_t current_button_mask_{0};
    std::size_t   last_active_notif_count_{0};
};

} // namespace winios::graphics
