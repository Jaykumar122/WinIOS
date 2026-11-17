#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "graphics/display_surface.h"

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Window Lifecycle States
// -----------------------------------------------------------------------------
enum class WindowState : std::uint8_t {
    Normal = 0,
    Minimized = 1,
    Hidden = 2,
    Closed = 3
};

// -----------------------------------------------------------------------------
// Window Metadata Structure
// -----------------------------------------------------------------------------
struct Window {
    std::uint32_t                   id{0};
    std::uint32_t                   owner_pid{0};
    std::string                     title;
    Rect                            frame{0, 0, 0, 0};
    std::int32_t                    z_order{0};
    WindowState                     state{WindowState::Normal};
    bool                            is_focused{false};
    std::shared_ptr<DisplaySurface> surface;

    [[nodiscard]] bool can_access(std::uint32_t caller_pid) const noexcept {
        return caller_pid == 0 || caller_pid == owner_pid;
    }

    [[nodiscard]] bool is_visible() const noexcept {
        return state == WindowState::Normal && surface && surface->is_visible();
    }
};

// -----------------------------------------------------------------------------
// WindowManager: Window Metadata, Focus, Z-Order & Hit-Testing
// -----------------------------------------------------------------------------
class WindowManager {
public:
    WindowManager() = default;
    ~WindowManager() = default;

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Window Creation & Destruction
    std::uint32_t create_window(
        std::uint32_t owner_pid,
        const std::string& title,
        const Rect& frame,
        std::shared_ptr<DisplaySurface> surface);

    bool destroy_window(std::uint32_t caller_pid, std::uint32_t window_id);

    // Focus Management
    bool set_focus(std::uint32_t window_id);
    [[nodiscard]] std::uint32_t focused_window_id() const noexcept;
    [[nodiscard]] std::uint32_t focused_pid() const noexcept;

    // State, Geometry & Z-Order
    bool set_window_state(std::uint32_t caller_pid, std::uint32_t window_id, WindowState state);
    bool set_window_frame(std::uint32_t caller_pid, std::uint32_t window_id, const Rect& frame);
    bool set_window_z_order(std::uint32_t caller_pid, std::uint32_t window_id, std::int32_t z);

    // Window Queries
    [[nodiscard]] std::shared_ptr<Window> get_window(std::uint32_t window_id) const;
    [[nodiscard]] std::vector<std::shared_ptr<Window>> get_windows_for_process(std::uint32_t pid) const;
    [[nodiscard]] std::vector<std::shared_ptr<Window>> get_visible_windows_z_ordered() const;
    [[nodiscard]] std::uint32_t window_count() const noexcept;

    // Hit Testing (frontmost window under coordinate)
    [[nodiscard]] std::shared_ptr<Window> hit_test(std::uint32_t px, std::uint32_t py) const;

    // Process Termination Cleanup
    void cleanup_process_windows(std::uint32_t pid);

private:
    void reassign_focus_locked();

    mutable std::mutex                           lock_;
    std::uint32_t                                next_window_id_{1};
    std::uint32_t                                focused_window_id_{0};
    std::map<std::uint32_t, std::shared_ptr<Window>> windows_;
};

} // namespace winios::graphics
