#include "graphics/desktop_controller.h"

#include <algorithm>
#include <cstring>

namespace winios::graphics {

DesktopController::DesktopController(
    std::shared_ptr<WindowManager> window_mgr,
    std::shared_ptr<MultiWindowCompositor> compositor,
    std::shared_ptr<VirtualDisplay> display,
    std::shared_ptr<devices::VirtualDisplayService> disp_svc,
    std::shared_ptr<devices::VirtualPointerDevice> ptr_dev,
    std::shared_ptr<devices::VirtualKeyboardDevice> kbd_dev,
    std::shared_ptr<devices::VirtualNotificationService> notif_svc)
    : window_mgr_(std::move(window_mgr))
    , compositor_(std::move(compositor))
    , display_(std::move(display))
    , disp_svc_(std::move(disp_svc))
    , ptr_dev_(std::move(ptr_dev))
    , kbd_dev_(std::move(kbd_dev))
    , notif_svc_(std::move(notif_svc)) {
}

void DesktopController::release_capture() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    is_dragging_ = false;
    dragging_window_handle_ = 0;
    captured_window_handle_ = 0;
    current_button_mask_ = 0;
}

bool DesktopController::handle_pointer_move(std::int32_t x, std::int32_t y, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> guard(lock_);
    last_pointer_x_ = x;
    last_pointer_y_ = y;
    ++stats_.total_pointer_events;

    // 1. Check if window dragging is active
    if (is_dragging_ && window_mgr_) {
        auto win = window_mgr_->get_window_by_handle(dragging_window_handle_);
        if (!win || win->state == WindowState::Closed) {
            is_dragging_ = false;
            dragging_window_handle_ = 0;
        } else {
            const std::int32_t dx = x - drag_start_pointer_x_;
            const std::int32_t dy = y - drag_start_pointer_y_;
            const std::int32_t new_x = std::max(0, static_cast<std::int32_t>(drag_start_window_frame_.x) + dx);
            const std::int32_t new_y = std::max(0, static_cast<std::int32_t>(drag_start_window_frame_.y) + dy);

            Rect new_frame{
                static_cast<std::uint32_t>(new_x),
                static_cast<std::uint32_t>(new_y),
                win->frame.width,
                win->frame.height
            };
            window_mgr_->set_window_frame(win->owner_pid, win->id, new_frame);
            ++stats_.total_drag_moves;
            return true;
        }
    }

    // 2. Route move event to captured or hit window
    std::shared_ptr<Window> target_win = nullptr;
    if (captured_window_handle_ != 0 && window_mgr_) {
        target_win = window_mgr_->get_window_by_handle(captured_window_handle_);
    }
    if (!target_win && window_mgr_) {
        target_win = window_mgr_->hit_test(static_cast<std::uint32_t>(std::max(0, x)),
                                          static_cast<std::uint32_t>(std::max(0, y)));
    }

    if (target_win && target_win->state != WindowState::Closed) {
        const Rect cf = target_win->client_frame();
        DesktopEvent ev{};
        ev.event_type = static_cast<std::uint32_t>(DesktopEventType::PointerMove);
        ev.flags = 0;
        ev.timestamp_us = timestamp_us;
        ev.window_handle = target_win->handle();
        ev.x = x - static_cast<std::int32_t>(cf.x);
        ev.y = y - static_cast<std::int32_t>(cf.y);
        ev.screen_x = x;
        ev.screen_y = y;
        ev.button_or_key = current_button_mask_;
        ev.modifiers = kbd_dev_ ? kbd_dev_->get_state().active_modifiers : 0;
        target_win->event_queue.push(ev);
    }

    return true;
}

bool DesktopController::handle_pointer_button(
    std::uint32_t button,
    bool is_down,
    std::int32_t x,
    std::int32_t y,
    std::uint64_t timestamp_us) {

    std::lock_guard<std::mutex> guard(lock_);
    last_pointer_x_ = x;
    last_pointer_y_ = y;
    ++stats_.total_pointer_events;

    if (is_down) {
        current_button_mask_ |= (1u << (button - 1));
    } else {
        current_button_mask_ &= ~(1u << (button - 1));
    }

    if (!window_mgr_) return false;

    const std::uint32_t ux = static_cast<std::uint32_t>(std::max(0, x));
    const std::uint32_t uy = static_cast<std::uint32_t>(std::max(0, y));

    if (is_down) {
        auto win = window_mgr_->hit_test(ux, uy);
        if (!win || win->state == WindowState::Closed) {
            // Click on desktop background
            window_mgr_->set_focus(0);
            return true;
        }

        NonClientZone zone = window_mgr_->hit_test_non_client(win->id, ux, uy);

        // Control buttons
        if (zone == NonClientZone::CloseButton && button == 1) {
            DesktopEvent ev{};
            ev.event_type = static_cast<std::uint32_t>(DesktopEventType::WindowCloseRequest);
            ev.timestamp_us = timestamp_us;
            ev.window_handle = win->handle();
            win->event_queue.push(ev);
            return true;
        }

        if (zone == NonClientZone::MinimizeButton && button == 1) {
            window_mgr_->minimize_window(win->owner_pid, win->handle());
            return true;
        }

        if (zone == NonClientZone::MaximizeButton && button == 1) {
            if (win->state == WindowState::Maximized) {
                window_mgr_->restore_window(win->owner_pid, win->handle());
            } else {
                const std::uint32_t dw = display_ ? display_->width() : 1290;
                const std::uint32_t dh = display_ ? display_->height() : 2796;
                window_mgr_->maximize_window(win->owner_pid, win->handle(), Rect{0, 0, dw, dh});
            }
            return true;
        }

        if (zone == NonClientZone::TitleBar && button == 1) {
            window_mgr_->raise_to_top(win->id);
            is_dragging_ = true;
            dragging_window_handle_ = win->handle();
            drag_start_pointer_x_ = x;
            drag_start_pointer_y_ = y;
            drag_start_window_frame_ = win->frame;
            return true;
        }

        // Client area or general click: activate and capture
        window_mgr_->raise_to_top(win->id);
        captured_window_handle_ = win->handle();

        const Rect cf = win->client_frame();
        DesktopEvent ev{};
        ev.event_type = static_cast<std::uint32_t>(DesktopEventType::PointerDown);
        ev.timestamp_us = timestamp_us;
        ev.window_handle = win->handle();
        ev.x = x - static_cast<std::int32_t>(cf.x);
        ev.y = y - static_cast<std::int32_t>(cf.y);
        ev.screen_x = x;
        ev.screen_y = y;
        ev.button_or_key = button;
        ev.modifiers = kbd_dev_ ? kbd_dev_->get_state().active_modifiers : 0;
        win->event_queue.push(ev);
    } else {
        // Button release
        if (is_dragging_) {
            is_dragging_ = false;
            dragging_window_handle_ = 0;
        }

        std::shared_ptr<Window> win = nullptr;
        if (captured_window_handle_ != 0) {
            win = window_mgr_->get_window_by_handle(captured_window_handle_);
        }
        if (!win) {
            win = window_mgr_->hit_test(ux, uy);
        }

        if (win && win->state != WindowState::Closed) {
            const Rect cf = win->client_frame();
            DesktopEvent ev{};
            ev.event_type = static_cast<std::uint32_t>(DesktopEventType::PointerUp);
            ev.timestamp_us = timestamp_us;
            ev.window_handle = win->handle();
            ev.x = x - static_cast<std::int32_t>(cf.x);
            ev.y = y - static_cast<std::int32_t>(cf.y);
            ev.screen_x = x;
            ev.screen_y = y;
            ev.button_or_key = button;
            ev.modifiers = kbd_dev_ ? kbd_dev_->get_state().active_modifiers : 0;
            win->event_queue.push(ev);
        }

        if (current_button_mask_ == 0) {
            captured_window_handle_ = 0;
        }
    }

    return true;
}

bool DesktopController::handle_pointer_wheel(std::int32_t delta, std::int32_t x, std::int32_t y, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!window_mgr_) return false;
    ++stats_.total_pointer_events;

    std::shared_ptr<Window> win = nullptr;
    if (captured_window_handle_ != 0) {
        win = window_mgr_->get_window_by_handle(captured_window_handle_);
    }
    if (!win) {
        win = window_mgr_->hit_test(static_cast<std::uint32_t>(std::max(0, x)),
                                    static_cast<std::uint32_t>(std::max(0, y)));
    }

    if (win && win->state != WindowState::Closed) {
        const Rect cf = win->client_frame();
        DesktopEvent ev{};
        ev.event_type = static_cast<std::uint32_t>(DesktopEventType::PointerWheel);
        ev.timestamp_us = timestamp_us;
        ev.window_handle = win->handle();
        ev.x = x - static_cast<std::int32_t>(cf.x);
        ev.y = y - static_cast<std::int32_t>(cf.y);
        ev.screen_x = x;
        ev.screen_y = y;
        ev.wheel_delta = delta;
        ev.modifiers = kbd_dev_ ? kbd_dev_->get_state().active_modifiers : 0;
        win->event_queue.push(ev);
    }
    return true;
}

bool DesktopController::handle_key_event(std::uint32_t vkey, bool is_down, std::uint32_t modifiers, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!window_mgr_) return false;
    ++stats_.total_keyboard_events;

    DesktopEvent ev{};
    ev.event_type = static_cast<std::uint32_t>(is_down ? DesktopEventType::KeyDown : DesktopEventType::KeyUp);
    ev.timestamp_us = timestamp_us;
    ev.button_or_key = vkey;
    ev.modifiers = modifiers;

    return window_mgr_->post_event_to_focused(ev);
}

bool DesktopController::handle_text_input(const std::string& utf8_text, std::uint64_t timestamp_us) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!window_mgr_ || utf8_text.empty()) return false;
    ++stats_.total_keyboard_events;

    DesktopEvent ev{};
    ev.event_type = static_cast<std::uint32_t>(DesktopEventType::TextInput);
    ev.timestamp_us = timestamp_us;
    ev.payload_len = static_cast<std::uint32_t>(std::min(utf8_text.size(), sizeof(ev.payload) - 1));
    std::memcpy(ev.payload, utf8_text.data(), ev.payload_len);
    ev.payload[ev.payload_len] = 0;

    return window_mgr_->post_event_to_focused(ev);
}

void DesktopController::render_notification_overlay() {
    if (!notif_svc_ || !display_) return;

    std::uint8_t* dst = display_->raw_data();
    std::uint32_t dst_w = display_->width();
    std::uint32_t dst_h = display_->height();
    std::uint32_t stride = display_->stride();
    if (!dst || dst_w == 0 || dst_h == 0) return;

    auto notifs = notif_svc_->get_active_notifications(0);
    if (notifs.empty()) return;

    // Render up to 3 topmost notification banners at the top right
    const std::size_t count = std::min<std::size_t>(notifs.size(), 3);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& n = notifs[i];
        const std::uint32_t card_w = 320;
        const std::uint32_t card_h = 65;
        const std::uint32_t card_x = (dst_w > card_w + 20) ? (dst_w - card_w - 20) : 10;
        const std::uint32_t card_y = 35 + static_cast<std::uint32_t>(i * (card_h + 10));

        // Dark banner background (ARGB 0xE624242A)
        const std::uint32_t bg_col = 0xE624242A;
        const std::uint32_t border_col = 0xFF404048;
        const std::string_view cat_view(n.category);
        const std::uint32_t badge_col = (cat_view == "system" || cat_view == "alert") ? 0xFFFF9500 : 0xFF0A84FF;

        // Draw card background & border
        for (std::uint32_t y = card_y; y < card_y + card_h && y < dst_h; ++y) {
            std::uint8_t* row = dst + (static_cast<std::size_t>(y) * stride) + (static_cast<std::size_t>(card_x) * 4);
            for (std::uint32_t x = card_x; x < card_x + card_w && x < dst_w; ++x) {
                bool is_border = (x == card_x || x == card_x + card_w - 1 || y == card_y || y == card_y + card_h - 1);
                std::uint32_t col = is_border ? border_col : bg_col;

                // Simple blend
                row[0] = static_cast<std::uint8_t>(col & 0xFF);
                row[1] = static_cast<std::uint8_t>((col >> 8) & 0xFF);
                row[2] = static_cast<std::uint8_t>((col >> 16) & 0xFF);
                row[3] = 255;
                row += 4;
            }
        }

        // Draw vertical color badge strip on left edge of toast
        for (std::uint32_t y = card_y + 4; y < card_y + card_h - 4 && y < dst_h; ++y) {
            std::uint8_t* row = dst + (static_cast<std::size_t>(y) * stride) + (static_cast<std::size_t>(card_x + 4) * 4);
            for (std::uint32_t x = card_x + 4; x < card_x + 8 && x < dst_w; ++x) {
                row[0] = static_cast<std::uint8_t>(badge_col & 0xFF);
                row[1] = static_cast<std::uint8_t>((badge_col >> 8) & 0xFF);
                row[2] = static_cast<std::uint8_t>((badge_col >> 16) & 0xFF);
                row[3] = 255;
                row += 4;
            }
        }
        ++stats_.notifications_rendered;
    }
}

bool DesktopController::step(std::uint64_t delta_us) {
    (void)delta_us;
    std::lock_guard<std::mutex> guard(lock_);

    std::size_t active_notifs = notif_svc_ ? notif_svc_->active_count() : 0;
    bool notifs_changed = (active_notifs != last_active_notif_count_);
    last_active_notif_count_ = active_notifs;

    // 1. Run compositor pass
    if (compositor_) {
        bool comp_ok = compositor_->composite(notifs_changed);
        if (comp_ok) {
            ++stats_.total_composited_frames;
        }
    }

    // 2. Render notification toasts on top of composited frame
    render_notification_overlay();

    return true;
}

void DesktopController::on_display_reconfigured(std::uint32_t new_width, std::uint32_t new_height, float new_scale) {
    (void)new_scale;
    std::lock_guard<std::mutex> guard(lock_);
    if (compositor_) {
        compositor_->invalidate_full_desktop();
    }
    // Release capture on resize
    is_dragging_ = false;
    dragging_window_handle_ = 0;
    captured_window_handle_ = 0;

    // Clamp windows within new bounds
    if (window_mgr_) {
        auto windows = window_mgr_->get_visible_windows_z_ordered();
        for (const auto& w : windows) {
            if (w->frame.x + w->frame.width > new_width || w->frame.y + w->frame.height > new_height) {
                std::uint32_t nx = std::min(w->frame.x, (new_width > w->frame.width) ? (new_width - w->frame.width) : 0);
                std::uint32_t ny = std::min(w->frame.y, (new_height > w->frame.height) ? (new_height - w->frame.height) : 0);
                window_mgr_->set_window_frame(w->owner_pid, w->id, Rect{nx, ny, w->frame.width, w->frame.height});
            }
        }
    }
}

} // namespace winios::graphics
