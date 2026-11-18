#pragma once

#include "settings/system_state_manager.h"
#include <cstdint>
#include <mutex>

namespace winios::settings {

struct ControlCenterRect {
    int32_t x{460};
    int32_t y{32};
    int32_t width{320};
    int32_t height{360};

    [[nodiscard]] bool contains(int32_t px, int32_t py) const noexcept {
        return px >= x && px < (x + width) && py >= y && py < (y + height);
    }
};

class ControlCenterService {
public:
    explicit ControlCenterService(SystemStateManager* state_mgr = nullptr);
    ~ControlCenterService() = default;

    ControlCenterService(const ControlCenterService&) = delete;
    ControlCenterService& operator=(const ControlCenterService&) = delete;

    // Visibility
    [[nodiscard]] bool is_visible() const noexcept;
    void show() noexcept;
    void hide() noexcept;
    void toggle() noexcept;

    // Bounds & Geometry
    [[nodiscard]] ControlCenterRect get_bounds() const noexcept;
    void set_bounds(int32_t x, int32_t y, int32_t w, int32_t h) noexcept;

    // User Input Handling
    // Returns true if the event was consumed by the control center (inside panel or dismissed)
    bool handle_pointer_click(int32_t x, int32_t y) noexcept;
    bool handle_key_down(int32_t key_code) noexcept;

    // Quick Action Toggles (synchronizes directly with SystemStateManager)
    bool toggle_theme() noexcept;
    bool toggle_accent() noexcept;
    bool cycle_display_scale() noexcept;
    bool toggle_display_orientation() noexcept;
    bool toggle_audio_mute() noexcept;
    bool step_volume_up(uint32_t step = 10) noexcept;
    bool step_volume_down(uint32_t step = 10) noexcept;
    bool toggle_low_power_mode() noexcept;
    bool toggle_network_adapter() noexcept;
    bool toggle_do_not_disturb() noexcept;

    [[nodiscard]] bool is_do_not_disturb() const noexcept { return dnd_active_; }
    void set_do_not_disturb(bool dnd) noexcept { dnd_active_ = dnd; }

    void set_state_manager(SystemStateManager* sm) noexcept { state_mgr_ = sm; }

private:
    mutable std::recursive_mutex lock_;
    SystemStateManager*          state_mgr_{nullptr};
    ControlCenterRect            bounds_{460, 32, 320, 360};
    bool                         visible_{false};
    bool                         dnd_active_{false};
};

} // namespace winios::settings
