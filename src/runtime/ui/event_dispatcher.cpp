#include "runtime/ui/event_dispatcher.h"

namespace winios::runtime::ui {

EventDispatcher::EventDispatcher(ViewGroup* root)
    : root_(root) {}

bool EventDispatcher::request_focus(View* view) {
    if (!view || !view->is_enabled() || view->visibility() != Visibility::Visible) {
        return false;
    }
    if (focused_view_ == view) return true;

    if (focused_view_) {
        focused_view_->set_focused(false);
    }
    focused_view_ = view;
    focused_view_->set_focused(true);
    return true;
}

void EventDispatcher::clear_focus() {
    if (focused_view_) {
        focused_view_->set_focused(false);
        focused_view_ = nullptr;
    }
}

void EventDispatcher::on_view_destroyed(View* view) {
    if (focused_view_ == view) {
        clear_focus();
    }
    if (captured_view_ == view) {
        release_capture();
    }
}

bool EventDispatcher::dispatch_touch_down(std::int32_t screen_x, std::int32_t screen_y) {
    if (!root_) return false;

    View* target = root_->hit_test(screen_x, screen_y);
    if (!target || !target->is_enabled() || target->visibility() != Visibility::Visible) {
        return false;
    }

    request_focus(target);
    captured_view_ = target;
    target->set_pressed(true);
    return true;
}

bool EventDispatcher::dispatch_touch_move(std::int32_t screen_x, std::int32_t screen_y) {
    if (captured_view_) {
        Rect abs_f = captured_view_->absolute_frame();
        const bool inside = abs_f.contains(screen_x, screen_y);
        captured_view_->set_pressed(inside);
        return true;
    }
    return false;
}

bool EventDispatcher::dispatch_touch_up(std::int32_t screen_x, std::int32_t screen_y) {
    if (!captured_view_) return false;

    View* target = captured_view_;
    captured_view_ = nullptr;
    target->set_pressed(false);

    Rect abs_f = target->absolute_frame();
    if (abs_f.contains(screen_x, screen_y) && target->is_enabled()) {
        target->perform_click();
        return true;
    }
    return false;
}

void EventDispatcher::dispatch_touch_cancel() {
    if (captured_view_) {
        captured_view_->set_pressed(false);
        captured_view_ = nullptr;
    }
}

} // namespace winios::runtime::ui
