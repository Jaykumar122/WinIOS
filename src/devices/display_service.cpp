#include "devices/display_service.h"
#include "security/capability_manager.h"

namespace winios::devices {

VirtualDisplayService::VirtualDisplayService(std::shared_ptr<graphics::VirtualDisplay> display,
                                             graphics::Compositor* comp,
                                             graphics::WindowManager* wm)
    : VirtualDeviceBase(1, 1, DeviceClass::Display, "Virtual Display Controller",
                        "WinIOS.Display.Software", 0x00010000,
                        static_cast<std::uint32_t>(security::Capability::DisplayWindow)),
      display_(std::move(display)),
      comp_(comp),
      wm_(wm) {
    if (display_) {
        const auto& cfg = display_->config();
        config_.width = cfg.width;
        config_.height = cfg.height;
        config_.scale = cfg.display_scale;
        config_.refresh_rate_hz = 60;
        config_.bits_per_pixel = 32;
        config_.orientation = static_cast<std::uint32_t>(cfg.orientation);
    } else {
        config_.width = 1179;
        config_.height = 2556;
        config_.scale = 3.0f;
        config_.refresh_rate_hz = 60;
        config_.bits_per_pixel = 32;
        config_.orientation = 0;
    }
}

DisplayConfigData VirtualDisplayService::get_config() const {
    std::lock_guard<std::mutex> guard(lock_);
    return config_;
}

bool VirtualDisplayService::set_config(const DisplayConfigData& new_cfg) {
    // Validation bounds
    if (new_cfg.width < 320 || new_cfg.width > 3840) return false;
    if (new_cfg.height < 240 || new_cfg.height > 3840) return false;
    if (new_cfg.scale < 0.5f || new_cfg.scale > 4.0f) return false;

    ResizeCallback cb_copy = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (state_ != DeviceState::Ready && state_ != DeviceState::Running) {
            return false;
        }

        config_ = new_cfg;
        ++resize_count_;

        if (display_) {
            graphics::DisplayConfiguration disp_cfg{};
            disp_cfg.width = new_cfg.width;
            disp_cfg.height = new_cfg.height;
            disp_cfg.display_scale = new_cfg.scale;
            disp_cfg.orientation = (new_cfg.orientation == 1) ?
                graphics::DisplayOrientation::LandscapeLeft :
                graphics::DisplayOrientation::Portrait;
            display_->reconfigure(disp_cfg);
        }

        if (comp_) {
            comp_->invalidate_full_frame();
        }

        cb_copy = resize_cb_;
    }

    if (cb_copy) {
        cb_copy(new_cfg.width, new_cfg.height);
    }

    return true;
}

void VirtualDisplayService::set_resize_callback(ResizeCallback cb) {
    std::lock_guard<std::mutex> guard(lock_);
    resize_cb_ = std::move(cb);
}

} // namespace winios::devices
