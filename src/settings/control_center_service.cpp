#include "settings/control_center_service.h"

namespace winios::settings {

ControlCenterService::ControlCenterService(SystemStateManager* state_mgr)
    : state_mgr_(state_mgr) {}

bool ControlCenterService::is_visible() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return visible_;
}

void ControlCenterService::show() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    visible_ = true;
}

void ControlCenterService::hide() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    visible_ = false;
}

void ControlCenterService::toggle() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    visible_ = !visible_;
}

ControlCenterRect ControlCenterService::get_bounds() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    return bounds_;
}

void ControlCenterService::set_bounds(int32_t x, int32_t y, int32_t w, int32_t h) noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    bounds_.x = x;
    bounds_.y = y;
    bounds_.width = (w > 100) ? w : 100;
    bounds_.height = (h > 100) ? h : 100;
}

bool ControlCenterService::handle_pointer_click(int32_t x, int32_t y) noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!visible_) {
        return false;
    }

    if (bounds_.contains(x, y)) {
        // Clicked inside Control Center card; event consumed
        return true;
    }

    // Clicked outside Control Center card; dismiss panel
    visible_ = false;
    return true; // consumed as dismissal
}

bool ControlCenterService::handle_key_down(int32_t key_code) noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!visible_) {
        return false;
    }

    // 27 = Escape key
    if (key_code == 27) {
        visible_ = false;
        return true;
    }
    return false;
}

bool ControlCenterService::toggle_theme() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto app = state_mgr_->get_appearance();
    ThemeMode next = (app.theme == ThemeMode::Dark) ? ThemeMode::Light : ThemeMode::Dark;
    return state_mgr_->set_theme(next);
}

bool ControlCenterService::toggle_accent() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto app = state_mgr_->get_appearance();
    uint32_t next_raw = (static_cast<uint32_t>(app.accent) + 1) % 8;
    return state_mgr_->set_accent(static_cast<AccentColor>(next_raw));
}

bool ControlCenterService::cycle_display_scale() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto disp = state_mgr_->get_display();
    DisplayScale next;
    switch (disp.scale) {
        case DisplayScale::Scale100: next = DisplayScale::Scale125; break;
        case DisplayScale::Scale125: next = DisplayScale::Scale150; break;
        case DisplayScale::Scale150: next = DisplayScale::Scale200; break;
        case DisplayScale::Scale200:
        default:                     next = DisplayScale::Scale100; break;
    }
    return state_mgr_->set_display_scale(next);
}

bool ControlCenterService::toggle_display_orientation() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto disp = state_mgr_->get_display();
    DisplayOrientation next = (disp.orientation == DisplayOrientation::Landscape)
                                  ? DisplayOrientation::Portrait
                                  : DisplayOrientation::Landscape;
    return state_mgr_->set_display_orientation(next);
}

bool ControlCenterService::toggle_audio_mute() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto aud = state_mgr_->get_audio();
    return state_mgr_->set_audio_muted(!aud.is_muted);
}

bool ControlCenterService::step_volume_up(uint32_t step) noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto aud = state_mgr_->get_audio();
    uint32_t new_vol = aud.master_volume + step;
    if (new_vol > 100) new_vol = 100;
    return state_mgr_->set_audio_volume(new_vol);
}

bool ControlCenterService::step_volume_down(uint32_t step) noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto aud = state_mgr_->get_audio();
    uint32_t new_vol = (aud.master_volume > step) ? (aud.master_volume - step) : 0;
    return state_mgr_->set_audio_volume(new_vol);
}

bool ControlCenterService::toggle_low_power_mode() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto pwr = state_mgr_->get_power();
    return state_mgr_->set_low_power_mode(!pwr.low_power_mode);
}

bool ControlCenterService::toggle_network_adapter() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    if (!state_mgr_) return false;
    auto net = state_mgr_->get_network();
    return state_mgr_->set_network_adapter_enabled(!net.adapter_enabled);
}

bool ControlCenterService::toggle_do_not_disturb() noexcept {
    std::lock_guard<std::recursive_mutex> lock(lock_);
    dnd_active_ = !dnd_active_;
    return true;
}

} // namespace winios::settings
