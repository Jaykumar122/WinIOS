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
// Window Lifecycle States & Non-Client Zones
// -----------------------------------------------------------------------------
enum class WindowState : std::uint8_t {
    Normal = 0,
    Minimized = 1,
    Hidden = 2,
    Closed = 3
};

enum class NonClientZone : std::uint8_t {
    None           = 0,
    Client         = 1,
    TitleBar       = 2,
    CloseButton    = 3,
    MinimizeButton = 4,
    MaximizeButton = 5,
    Border         = 6
};

// -----------------------------------------------------------------------------
// Window Metadata Structure
// -----------------------------------------------------------------------------
struct Window {
    static constexpr std::uint32_t  kTitleBarHeight = 28;
    static constexpr std::uint32_t  kBorderWidth = 1;
    static constexpr std::uint32_t  kButtonWidth = 18;
    static constexpr std::uint32_t  kButtonHeight = 18;
    static constexpr std::uint32_t  kButtonMargin = 6;

    std::uint32_t                   id{0};
    std::uint32_t                   owner_pid{0};
    std::string                     title;
    Rect                            frame{0, 0, 0, 0};
    std::int32_t                    z_order{0};
    WindowState                     state{WindowState::Normal};
    bool                            is_focused{false};
    bool                            always_on_top{false};
    std::uint32_t                   min_width{100};
    std::uint32_t                   min_height{80};
    std::uint32_t                   max_width{3840};
    std::uint32_t                   max_height{2160};
    bool                            is_dirty{true};
    Rect                            dirty_rect{0, 0, 0, 0};
    std::shared_ptr<DisplaySurface> surface;

    [[nodiscard]] bool can_access(std::uint32_t caller_pid) const noexcept {
        return caller_pid == 0 || caller_pid == owner_pid;
    }

    [[nodiscard]] bool is_visible() const noexcept {
        return state == WindowState::Normal && surface && surface->is_visible();
    }

    [[nodiscard]] bool is_minimized() const noexcept {
        return state == WindowState::Minimized;
    }

    [[nodiscard]] Rect client_frame() const noexcept {
        const std::uint32_t bw = kBorderWidth;
        const std::uint32_t tbh = kTitleBarHeight;
        const std::uint32_t cx = frame.x + bw;
        const std::uint32_t cy = frame.y + tbh;
        const std::uint32_t cw = (frame.width > (bw * 2)) ? (frame.width - (bw * 2)) : 0;
        const std::uint32_t ch = (frame.height > (tbh + bw)) ? (frame.height - (tbh + bw)) : 0;
        return Rect{cx, cy, cw, ch};
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

    // Stacking & Z-Order Management
    bool raise_to_top(std::uint32_t window_id);
    bool lower_to_bottom(std::uint32_t window_id);
    bool set_always_on_top(std::uint32_t caller_pid, std::uint32_t window_id, bool on_top);

    // Window Movement & Resizing
    bool move_window(std::uint32_t caller_pid, std::uint32_t window_id, std::int32_t dx, std::int32_t dy);
    bool resize_window(std::uint32_t caller_pid, std::uint32_t window_id, std::uint32_t new_width, std::uint32_t new_height);

    // Non-Client Zone Hit-Testing
    [[nodiscard]] NonClientZone hit_test_non_client(std::uint32_t window_id, std::uint32_t px, std::uint32_t py) const;

    // Damage Tracking
    void mark_window_dirty(std::uint32_t window_id, const Rect& damage);
    void clear_window_dirty(std::uint32_t window_id);
    [[nodiscard]] bool is_window_dirty(std::uint32_t window_id) const;
    [[nodiscard]] Rect get_window_dirty_rect(std::uint32_t window_id) const;

    // Window Queries
    [[nodiscard]] std::shared_ptr<Window> get_window(std::uint32_t window_id) const;
    [[nodiscard]] std::vector<std::shared_ptr<Window>> get_windows_for_process(std::uint32_t pid) const;
    [[nodiscard]] std::vector<std::shared_ptr<Window>> get_visible_windows_z_ordered() const;
    [[nodiscard]] std::uint32_t window_count() const noexcept;

    // Hit Testing (frontmost window under coordinate)
    [[nodiscard]] std::shared_ptr<Window> hit_test(std::uint32_t px, std::uint32_t py) const;

    // Process Termination Cleanup
    void cleanup_process_windows(std::uint32_t pid);
    void destroy_windows_for_process(std::uint32_t pid) { cleanup_process_windows(pid); }

private:
    void reassign_focus_locked();

    mutable std::mutex                           lock_;
    std::uint32_t                                next_window_id_{1};
    std::uint32_t                                focused_window_id_{0};
    std::map<std::uint32_t, std::shared_ptr<Window>> windows_;
};

} // namespace winios::graphics
