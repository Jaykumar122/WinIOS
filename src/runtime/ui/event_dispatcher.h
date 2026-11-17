#pragma once

#include <cstdint>
#include <memory>

#include "runtime/ui/view.h"
#include "runtime/ui/view_group.h"
#include "runtime/ui/ui_types.h"

namespace winios::runtime::ui {

class EventDispatcher {
public:
    explicit EventDispatcher(ViewGroup* root = nullptr);

    void set_root(ViewGroup* root) noexcept { root_ = root; }
    [[nodiscard]] ViewGroup* root() const noexcept { return root_; }

    [[nodiscard]] View* focused_view() const noexcept { return focused_view_; }
    bool request_focus(View* view);
    void clear_focus();

    [[nodiscard]] View* captured_view() const noexcept { return captured_view_; }
    void set_capture(View* view) noexcept { captured_view_ = view; }
    void release_capture() noexcept { captured_view_ = nullptr; }

    bool dispatch_touch_down(std::int32_t screen_x, std::int32_t screen_y);
    bool dispatch_touch_move(std::int32_t screen_x, std::int32_t screen_y);
    bool dispatch_touch_up(std::int32_t screen_x, std::int32_t screen_y);
    void dispatch_touch_cancel();

    void on_view_destroyed(View* view);

private:
    ViewGroup* root_{nullptr};
    View* focused_view_{nullptr};
    View* captured_view_{nullptr};
};

} // namespace winios::runtime::ui
