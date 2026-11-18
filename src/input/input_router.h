#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include "graphics/window_manager.h"
#include "graphics/virtual_display.h"
#include "input/touch_event_queue.h"
#include "input/virtual_input_device.h"

namespace winios::input {

// -----------------------------------------------------------------------------
// TouchCaptureRecord & MouseCaptureRecord: Contact & Pointer Capture Mapping
// -----------------------------------------------------------------------------
struct TouchCaptureRecord {
    bool          is_captured{false};
    std::uint32_t owner_pid{0};
    std::uint32_t window_id{0};
};

struct MouseCaptureRecord {
    bool          is_captured{false};
    std::uint32_t owner_pid{0};
    std::uint32_t window_id{0};
    MouseButton   button{MouseButton::None};
};

// -----------------------------------------------------------------------------
// InputRouter: Coordinate Mapping, Hit-Testing, Contact Capture & Per-PID Queuing
// -----------------------------------------------------------------------------
class InputRouter {
public:
    explicit InputRouter(std::shared_ptr<graphics::WindowManager> window_manager);
    ~InputRouter() = default;

    InputRouter(const InputRouter&) = delete;
    InputRouter& operator=(const InputRouter&) = delete;

    // Coordinate translation helper (Host Window Pixels -> Virtual Display Coordinates)
    static TouchPoint translate_host_coordinate(
        std::int32_t host_x,
        std::int32_t host_y,
        std::uint32_t host_window_w,
        std::uint32_t host_window_h,
        const graphics::DisplayConfiguration& disp_config) noexcept;

    // Ingest raw device event, perform hit-testing / capture routing, and enqueue to target PID queue
    bool route_event(const InputEvent& event);

    // Guest Process API: retrieve next event intended for caller_pid
    bool poll_event(std::uint32_t caller_pid, InputEvent& out_event);

    // Mouse Capture Management
    [[nodiscard]] bool is_mouse_captured() const noexcept;
    [[nodiscard]] std::uint32_t mouse_captured_window_id() const noexcept;
    [[nodiscard]] std::uint32_t mouse_captured_pid() const noexcept;
    void release_mouse_capture();

    // Lifecycle: release capture and queues when a window or process terminates
    void on_window_destroyed(std::uint32_t window_id);
    void on_process_terminated(std::uint32_t pid);

    // Diagnostics
    [[nodiscard]] std::uint64_t total_routed_events() const noexcept { return total_routed_; }
    [[nodiscard]] std::size_t pending_events_for_process(std::uint32_t pid) const noexcept;

private:
    std::shared_ptr<graphics::WindowManager>     window_mgr_;
    std::uint64_t                                total_routed_{0};
    mutable std::mutex                           lock_;
    TouchCaptureRecord                           captures_[VirtualInputDevice::kMaxTouchContacts]{};
    MouseCaptureRecord                           mouse_capture_{};
    std::map<std::uint32_t, std::shared_ptr<TouchEventQueue>> process_queues_;
};

} // namespace winios::input
