#pragma once

#include "devices/virtual_device_interface.h"
#include "graphics/virtual_display.h"
#include "graphics/compositor.h"
#include "graphics/window_manager.h"

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>

namespace winios::devices {

#pragma pack(push, 8)
struct DisplayConfigData {
    std::uint32_t width{1179};
    std::uint32_t height{2556};
    float         scale{3.0f};
    std::uint32_t refresh_rate_hz{60};
    std::uint32_t bits_per_pixel{32};
    std::uint32_t orientation{0}; // 0 = Portrait, 1 = Landscape
    std::uint32_t flags{0};
};
#pragma pack(pop)

class VirtualDisplayService : public VirtualDeviceBase {
public:
    using ResizeCallback = std::function<void(std::uint32_t new_w, std::uint32_t new_h)>;

    VirtualDisplayService(std::shared_ptr<graphics::VirtualDisplay> display = nullptr,
                          graphics::Compositor* comp = nullptr,
                          graphics::WindowManager* wm = nullptr);
    ~VirtualDisplayService() override = default;

    // Display Information
    [[nodiscard]] DisplayConfigData get_config() const;
    [[nodiscard]] bool set_config(const DisplayConfigData& new_cfg);

    // Resize notification registration
    void set_resize_callback(ResizeCallback cb);

    // Metrics
    [[nodiscard]] std::uint64_t resize_count() const noexcept { return resize_count_; }
    [[nodiscard]] std::uint32_t current_width() const noexcept { return config_.width; }
    [[nodiscard]] std::uint32_t current_height() const noexcept { return config_.height; }

    void set_virtual_display(std::shared_ptr<graphics::VirtualDisplay> disp) { display_ = disp; }
    void set_compositor(graphics::Compositor* comp) { comp_ = comp; }
    void set_window_manager(graphics::WindowManager* wm) { wm_ = wm; }

private:
    mutable std::mutex lock_;
    DisplayConfigData config_{};
    std::shared_ptr<graphics::VirtualDisplay> display_{nullptr};
    graphics::Compositor* comp_{nullptr};
    graphics::WindowManager* wm_{nullptr};
    ResizeCallback resize_cb_{nullptr};
    std::uint64_t resize_count_{0};
};

} // namespace winios::devices
