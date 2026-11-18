#include "input/input_router.h"

#include <algorithm>
#include "input/input_provenance.h"
#include "kernel/guest_trace_logger.h"

namespace winios::input {

InputRouter::InputRouter(std::shared_ptr<graphics::WindowManager> window_manager)
    : window_mgr_(std::move(window_manager)) {
}

TouchPoint InputRouter::translate_host_coordinate(
    std::int32_t host_x,
    std::int32_t host_y,
    std::uint32_t host_window_w,
    std::uint32_t host_window_h,
    const graphics::DisplayConfiguration& disp_config) noexcept {

    TouchPoint tp{};
    if (host_window_w == 0 || host_window_h == 0 || disp_config.width == 0 || disp_config.height == 0) {
        return tp;
    }

    // Compute scale factors
    double scale_x = static_cast<double>(disp_config.width) / static_cast<double>(host_window_w);
    double scale_y = static_cast<double>(disp_config.height) / static_cast<double>(host_window_h);

    std::int32_t vx = static_cast<std::int32_t>(host_x * scale_x);
    std::int32_t vy = static_cast<std::int32_t>(host_y * scale_y);

    // Clamp to virtual display dimensions
    tp.x = std::clamp(vx, 0, static_cast<std::int32_t>(disp_config.width - 1));
    tp.y = std::clamp(vy, 0, static_cast<std::int32_t>(disp_config.height - 1));
    tp.is_active = true;
    return tp;
}

bool InputRouter::route_event(const InputEvent& event) {
    std::lock_guard<std::mutex> guard(lock_);
    InputEvent routed = event;
    std::uint32_t target_pid = event.target_pid;
    std::uint32_t target_wid = event.target_window_id;

    if (target_wid > 0) {
        if (window_mgr_) {
            auto win = window_mgr_->get_window(target_wid);
            if (!win) {
                kernel::GuestTraceLogger::instance().log_input_provenance(routed, InputDeliveryStatus::RejectedWindowDestroyed);
                return false;
            }
            target_pid = win->owner_pid;
        }
    } else {
        switch (event.type) {
        case InputEventType::TouchBegin: {
            std::uint32_t cid = event.touch.contact_id;
            if (cid < VirtualInputDevice::kMaxTouchContacts) {
                std::uint32_t px = static_cast<std::uint32_t>(std::max(0, event.touch.x));
                std::uint32_t py = static_cast<std::uint32_t>(std::max(0, event.touch.y));

                std::shared_ptr<graphics::Window> win = nullptr;
                if (window_mgr_) {
                    win = window_mgr_->hit_test(px, py);
                }

                if (win) {
                    target_pid = win->owner_pid;
                    target_wid = win->id;
                    captures_[cid].is_captured = true;
                    captures_[cid].owner_pid = target_pid;
                    captures_[cid].window_id = target_wid;

                    // Bring clicked window to focus
                    window_mgr_->set_focus(target_wid);
                } else {
                    // Background tap: route to PID 1 (Shell/System) if available
                    target_pid = 1;
                    target_wid = 0;
                    captures_[cid].is_captured = true;
                    captures_[cid].owner_pid = target_pid;
                    captures_[cid].window_id = 0;
                }
            }
            break;
        }

        case InputEventType::TouchMove: {
            std::uint32_t cid = event.touch.contact_id;
            if (cid < VirtualInputDevice::kMaxTouchContacts && captures_[cid].is_captured) {
                target_pid = captures_[cid].owner_pid;
                target_wid = captures_[cid].window_id;
            }
            break;
        }

        case InputEventType::TouchEnd:
        case InputEventType::TouchCancel: {
            std::uint32_t cid = event.touch.contact_id;
            if (cid < VirtualInputDevice::kMaxTouchContacts && captures_[cid].is_captured) {
                target_pid = captures_[cid].owner_pid;
                target_wid = captures_[cid].window_id;
                captures_[cid].is_captured = false;
                captures_[cid].owner_pid = 0;
                captures_[cid].window_id = 0;
            }
            break;
        }

        case InputEventType::MouseMove: {
            if (mouse_capture_.is_captured) {
                target_pid = mouse_capture_.owner_pid;
                target_wid = mouse_capture_.window_id;
            } else {
                std::uint32_t px = static_cast<std::uint32_t>(std::max(0, event.mouse.x));
                std::uint32_t py = static_cast<std::uint32_t>(std::max(0, event.mouse.y));
                std::shared_ptr<graphics::Window> win = nullptr;
                if (window_mgr_) {
                    win = window_mgr_->hit_test(px, py);
                }
                if (win) {
                    target_pid = win->owner_pid;
                    target_wid = win->id;
                } else if (window_mgr_) {
                    target_pid = window_mgr_->focused_pid();
                    target_wid = window_mgr_->focused_window_id();
                    if (target_pid == 0) target_pid = 1;
                } else {
                    target_pid = 1;
                }
            }
            break;
        }

        case InputEventType::MouseDown: {
            std::uint32_t px = static_cast<std::uint32_t>(std::max(0, event.mouse.x));
            std::uint32_t py = static_cast<std::uint32_t>(std::max(0, event.mouse.y));
            std::shared_ptr<graphics::Window> win = nullptr;
            if (window_mgr_) {
                win = window_mgr_->hit_test(px, py);
            }
            if (win) {
                target_pid = win->owner_pid;
                target_wid = win->id;
                mouse_capture_.is_captured = true;
                mouse_capture_.owner_pid = target_pid;
                mouse_capture_.window_id = target_wid;
                mouse_capture_.button = event.mouse.button;

                window_mgr_->set_focus(target_wid);
            } else {
                target_pid = 1;
                target_wid = 0;
                mouse_capture_.is_captured = true;
                mouse_capture_.owner_pid = target_pid;
                mouse_capture_.window_id = 0;
                mouse_capture_.button = event.mouse.button;
            }
            break;
        }

        case InputEventType::MouseUp: {
            if (mouse_capture_.is_captured) {
                target_pid = mouse_capture_.owner_pid;
                target_wid = mouse_capture_.window_id;
                mouse_capture_.is_captured = false;
                mouse_capture_.owner_pid = 0;
                mouse_capture_.window_id = 0;
                mouse_capture_.button = MouseButton::None;
            } else {
                std::uint32_t px = static_cast<std::uint32_t>(std::max(0, event.mouse.x));
                std::uint32_t py = static_cast<std::uint32_t>(std::max(0, event.mouse.y));
                std::shared_ptr<graphics::Window> win = nullptr;
                if (window_mgr_) {
                    win = window_mgr_->hit_test(px, py);
                }
                if (win) {
                    target_pid = win->owner_pid;
                    target_wid = win->id;
                } else {
                    target_pid = 1;
                }
            }
            break;
        }

        case InputEventType::MouseWheel: {
            std::uint32_t px = static_cast<std::uint32_t>(std::max(0, event.mouse.x));
            std::uint32_t py = static_cast<std::uint32_t>(std::max(0, event.mouse.y));
            std::shared_ptr<graphics::Window> win = nullptr;
            if (window_mgr_) {
                win = window_mgr_->hit_test(px, py);
            }
            if (win) {
                target_pid = win->owner_pid;
                target_wid = win->id;
            } else if (window_mgr_) {
                target_pid = window_mgr_->focused_pid();
                target_wid = window_mgr_->focused_window_id();
                if (target_pid == 0) target_pid = 1;
            } else {
                target_pid = 1;
            }
            break;
        }

        case InputEventType::KeyPress:
        case InputEventType::KeyRelease:
        case InputEventType::CharInput:
        case InputEventType::ButtonHome:
        case InputEventType::ButtonLock: {
            if (window_mgr_) {
                target_wid = window_mgr_->focused_window_id();
                target_pid = window_mgr_->focused_pid();
            }
            if (target_pid == 0) {
                target_pid = 1; // Default to Shell/PID 1
            }
            break;
        }

        case InputEventType::WindowFocus:
        case InputEventType::WindowBlur: {
            if (window_mgr_) {
                target_wid = window_mgr_->focused_window_id();
                target_pid = window_mgr_->focused_pid();
            }
            if (target_pid == 0) {
                target_pid = 1;
            }
            break;
        }
        }
    }

    if (target_pid == 0) {
        kernel::GuestTraceLogger::instance().log_input_provenance(routed, InputDeliveryStatus::RejectedNoTargetWindow);
        return false;
    }

    // Verify window still exists if target_wid > 0
    if (target_wid > 0 && window_mgr_) {
        auto win = window_mgr_->get_window(target_wid);
        if (!win) {
            kernel::GuestTraceLogger::instance().log_input_provenance(routed, InputDeliveryStatus::RejectedWindowDestroyed);
            return false;
        }
    }

    routed.target_pid = target_pid;
    routed.target_window_id = target_wid;

    // Dispatch into per-PID queue
    auto& q = process_queues_[target_pid];
    if (!q) {
        q = std::make_shared<TouchEventQueue>();
    }
    const bool pushed = q->push(routed);
    if (!pushed) {
        kernel::GuestTraceLogger::instance().log_input_provenance(routed, InputDeliveryStatus::DroppedQueueFull);
        return false;
    }

    kernel::GuestTraceLogger::instance().log_input_provenance(routed, InputDeliveryStatus::Delivered);
    total_routed_++;
    return true;
}

bool InputRouter::poll_event(std::uint32_t caller_pid, InputEvent& out_event) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_queues_.find(caller_pid);
    if (it == process_queues_.end() || !it->second) {
        return false;
    }
    return it->second->pop(out_event);
}

bool InputRouter::is_mouse_captured() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return mouse_capture_.is_captured;
}

std::uint32_t InputRouter::mouse_captured_window_id() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return mouse_capture_.window_id;
}

std::uint32_t InputRouter::mouse_captured_pid() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return mouse_capture_.owner_pid;
}

void InputRouter::release_mouse_capture() {
    std::lock_guard<std::mutex> guard(lock_);
    mouse_capture_.is_captured = false;
    mouse_capture_.owner_pid = 0;
    mouse_capture_.window_id = 0;
    mouse_capture_.button = MouseButton::None;
}

void InputRouter::on_window_destroyed(std::uint32_t window_id) {
    std::lock_guard<std::mutex> guard(lock_);
    for (std::uint32_t i = 0; i < VirtualInputDevice::kMaxTouchContacts; ++i) {
        if (captures_[i].is_captured && captures_[i].window_id == window_id) {
            // Synthesize TouchCancel event to the process before releasing
            InputEvent cancel_evt{};
            cancel_evt.type = InputEventType::TouchCancel;
            cancel_evt.target_pid = captures_[i].owner_pid;
            cancel_evt.target_window_id = window_id;
            cancel_evt.touch.contact_id = i;

            auto it = process_queues_.find(captures_[i].owner_pid);
            if (it != process_queues_.end() && it->second) {
                it->second->push(cancel_evt);
            }

            captures_[i].is_captured = false;
            captures_[i].owner_pid = 0;
            captures_[i].window_id = 0;
        }
    }

    if (mouse_capture_.is_captured && mouse_capture_.window_id == window_id) {
        mouse_capture_.is_captured = false;
        mouse_capture_.owner_pid = 0;
        mouse_capture_.window_id = 0;
        mouse_capture_.button = MouseButton::None;
    }
}

void InputRouter::on_process_terminated(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    for (std::uint32_t i = 0; i < VirtualInputDevice::kMaxTouchContacts; ++i) {
        if (captures_[i].is_captured && captures_[i].owner_pid == pid) {
            captures_[i].is_captured = false;
            captures_[i].owner_pid = 0;
            captures_[i].window_id = 0;
        }
    }

    if (mouse_capture_.is_captured && mouse_capture_.owner_pid == pid) {
        mouse_capture_.is_captured = false;
        mouse_capture_.owner_pid = 0;
        mouse_capture_.window_id = 0;
        mouse_capture_.button = MouseButton::None;
    }

    process_queues_.erase(pid);
}

std::size_t InputRouter::pending_events_for_process(std::uint32_t pid) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_queues_.find(pid);
    if (it != process_queues_.end() && it->second) {
        return it->second->size();
    }
    return 0;
}

} // namespace winios::input
