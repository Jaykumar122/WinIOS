#include "settings/appearance_service.h"

namespace winios::settings {

ColorRGBA AppearanceService::resolve_accent(AccentColor accent) {
    switch (accent) {
        case AccentColor::Blue:     return {0, 122, 255, 255};   // iOS System Blue
        case AccentColor::Purple:   return {175, 82, 222, 255};  // iOS Purple
        case AccentColor::Pink:     return {255, 45, 85, 255};   // iOS Pink
        case AccentColor::Red:      return {255, 59, 48, 255};   // iOS Red
        case AccentColor::Orange:   return {255, 149, 0, 255};   // iOS Orange
        case AccentColor::Yellow:   return {255, 204, 0, 255};   // iOS Yellow
        case AccentColor::Green:    return {52, 199, 89, 255};   // iOS Green
        case AccentColor::Graphite: return {142, 142, 147, 255}; // iOS Gray
        default:                    return {0, 122, 255, 255};
    }
}

ThemePalette AppearanceService::build_palette(const AppearanceSettings& app) {
    ThemePalette p{};
    p.is_dark = (app.theme != ThemeMode::Light);
    p.is_high_contrast = app.high_contrast;
    p.accent = resolve_accent(app.accent);

    if (p.is_high_contrast) {
        if (p.is_dark) {
            p.background        = {0, 0, 0, 255};
            p.surface           = {10, 10, 10, 255};
            p.titlebar          = {25, 25, 25, 255};
            p.titlebar_inactive = {15, 15, 15, 255};
            p.text_primary      = {255, 255, 255, 255};
            p.text_secondary    = {200, 200, 200, 255};
            p.border            = {255, 255, 255, 255};
            p.control_face      = {35, 35, 35, 255};
            p.control_text      = {255, 255, 255, 255};
        } else {
            p.background        = {255, 255, 255, 255};
            p.surface           = {245, 245, 245, 255};
            p.titlebar          = {230, 230, 230, 255};
            p.titlebar_inactive = {240, 240, 240, 255};
            p.text_primary      = {0, 0, 0, 255};
            p.text_secondary    = {40, 40, 40, 255};
            p.border            = {0, 0, 0, 255};
            p.control_face      = {220, 220, 220, 255};
            p.control_text      = {0, 0, 0, 255};
        }
    } else {
        if (p.is_dark) {
            p.background        = {28, 28, 30, 255};
            p.surface           = {44, 44, 46, 255};
            p.titlebar          = {58, 58, 60, 255};
            p.titlebar_inactive = {38, 38, 40, 255};
            p.text_primary      = {255, 255, 255, 255};
            p.text_secondary    = {174, 174, 178, 255};
            p.border            = {72, 72, 74, 255};
            p.control_face      = {58, 58, 60, 255};
            p.control_text      = {255, 255, 255, 255};
        } else {
            p.background        = {242, 242, 247, 255};
            p.surface           = {255, 255, 255, 255};
            p.titlebar          = {229, 229, 234, 255};
            p.titlebar_inactive = {242, 242, 247, 255};
            p.text_primary      = {0, 0, 0, 255};
            p.text_secondary    = {99, 99, 102, 255};
            p.border            = {199, 199, 204, 255};
            p.control_face      = {229, 229, 234, 255};
            p.control_text      = {0, 0, 0, 255};
        }
    }

    return p;
}

AppearanceService::AppearanceService(std::shared_ptr<SystemStateManager> state_mgr)
    : state_mgr_(std::move(state_mgr)) {
    if (state_mgr_) {
        state_mgr_->add_observer(this);
        current_palette_ = build_palette(state_mgr_->get_appearance());
    } else {
        AppearanceSettings def{};
        current_palette_ = build_palette(def);
    }
}

AppearanceService::~AppearanceService() {
    if (state_mgr_) {
        state_mgr_->remove_observer(this);
    }
}

ThemePalette AppearanceService::get_palette() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return current_palette_;
}

bool AppearanceService::is_dark_mode() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return current_palette_.is_dark;
}

ColorRGBA AppearanceService::get_accent_color() const {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return current_palette_.accent;
}

void AppearanceService::on_appearance_changed(const AppearanceSettings& app) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    current_palette_ = build_palette(app);
}

} // namespace winios::settings
