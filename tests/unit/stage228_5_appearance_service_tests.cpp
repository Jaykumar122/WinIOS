#include "settings/appearance_service.h"
#include <cassert>
#include <iostream>
#include <memory>

using namespace winios::settings;

static void test_palette_dark_mode() {
    auto mgr = std::make_shared<SystemStateManager>();
    mgr->set_theme(ThemeMode::Dark);
    mgr->set_accent(AccentColor::Blue);

    AppearanceService svc(mgr);
    ThemePalette pal = svc.get_palette();

    assert(pal.is_dark == true);
    assert(pal.background.r < 50 && pal.background.g < 50 && pal.background.b < 50);
    assert(pal.text_primary.r > 200 && pal.text_primary.g > 200 && pal.text_primary.b > 200);

    ColorRGBA blue = AppearanceService::resolve_accent(AccentColor::Blue);
    assert(pal.accent.r == blue.r && pal.accent.g == blue.g && pal.accent.b == blue.b);

    std::cout << "[PASS] test_palette_dark_mode\n";
}

static void test_palette_light_mode() {
    auto mgr = std::make_shared<SystemStateManager>();
    mgr->set_theme(ThemeMode::Light);
    mgr->set_accent(AccentColor::Orange);

    AppearanceService svc(mgr);
    ThemePalette pal = svc.get_palette();

    assert(pal.is_dark == false);
    assert(pal.background.r > 200 && pal.background.g > 200 && pal.background.b > 200);
    assert(pal.text_primary.r < 50 && pal.text_primary.g < 50 && pal.text_primary.b < 50);

    ColorRGBA orange = AppearanceService::resolve_accent(AccentColor::Orange);
    assert(pal.accent.r == orange.r && pal.accent.g == orange.g && pal.accent.b == orange.b);

    std::cout << "[PASS] test_palette_light_mode\n";
}

static void test_palette_high_contrast() {
    auto mgr = std::make_shared<SystemStateManager>();
    mgr->set_theme(ThemeMode::Dark);
    mgr->set_high_contrast(true);

    AppearanceService svc(mgr);
    ThemePalette pal = svc.get_palette();

    assert(pal.background.r == 0 && pal.background.g == 0 && pal.background.b == 0);
    assert(pal.text_primary.r == 255 && pal.text_primary.g == 255 && pal.text_primary.b == 255);

    std::cout << "[PASS] test_palette_high_contrast\n";
}

static void test_accent_colors() {
    for (uint32_t a = 0; a <= 7; ++a) {
        ColorRGBA col = AppearanceService::resolve_accent(static_cast<AccentColor>(a));
        assert(col.a == 255);
    }
    std::cout << "[PASS] test_accent_colors\n";
}

int main() {
    std::cout << "--- Stage 228.5 Appearance Service Tests ---\n";
    test_palette_dark_mode();
    test_palette_light_mode();
    test_palette_high_contrast();
    test_accent_colors();
    std::cout << "All Stage 228.5 Appearance Service Tests PASSED.\n";
    return 0;
}
