#pragma once

#include "settings/settings_types.h"
#include "settings/system_state_manager.h"

#include <cstdint>
#include <memory>
#include <mutex>

namespace winios::settings {

struct ColorRGBA {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};

    [[nodiscard]] constexpr std::uint32_t to_argb() const noexcept {
        return (static_cast<std::uint32_t>(a) << 24) |
               (static_cast<std::uint32_t>(r) << 16) |
               (static_cast<std::uint32_t>(g) << 8)  |
               static_cast<std::uint32_t>(b);
    }
};

struct ThemePalette {
    ColorRGBA background;
    ColorRGBA surface;
    ColorRGBA titlebar;
    ColorRGBA titlebar_inactive;
    ColorRGBA text_primary;
    ColorRGBA text_secondary;
    ColorRGBA accent;
    ColorRGBA border;
    ColorRGBA control_face;
    ColorRGBA control_text;
    bool      is_dark{true};
    bool      is_high_contrast{false};
};

class AppearanceService : public SystemStateObserver {
public:
    explicit AppearanceService(std::shared_ptr<SystemStateManager> state_mgr);
    ~AppearanceService() override;

    AppearanceService(const AppearanceService&) = delete;
    AppearanceService& operator=(const AppearanceService&) = delete;

    [[nodiscard]] ThemePalette get_palette() const;
    [[nodiscard]] bool is_dark_mode() const;
    [[nodiscard]] ColorRGBA get_accent_color() const;

    // SystemStateObserver overrides
    void on_appearance_changed(const AppearanceSettings& app) override;

    static ColorRGBA resolve_accent(AccentColor accent);
    static ThemePalette build_palette(const AppearanceSettings& app);

private:
    std::shared_ptr<SystemStateManager> state_mgr_;
    mutable std::recursive_mutex        lock_;
    ThemePalette                        current_palette_;
};

} // namespace winios::settings
