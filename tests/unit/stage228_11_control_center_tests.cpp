#include "settings/control_center_service.h"
#include <cassert>
#include <iostream>

using namespace winios::settings;

static void test_visibility_and_geometry() {
    SystemStateManager mgr;
    ControlCenterService cc(&mgr);

    assert(!cc.is_visible());
    cc.show();
    assert(cc.is_visible());
    cc.hide();
    assert(!cc.is_visible());
    cc.toggle();
    assert(cc.is_visible());

    auto bounds = cc.get_bounds();
    assert(bounds.width == 320);
    assert(bounds.height == 360);
    assert(bounds.contains(bounds.x + 10, bounds.y + 10));
    assert(!bounds.contains(bounds.x - 10, bounds.y));
    std::cout << "[PASS] test_visibility_and_geometry\n";
}

static void test_input_handling_and_dismissal() {
    SystemStateManager mgr;
    ControlCenterService cc(&mgr);

    cc.show();
    assert(cc.is_visible());

    // Click inside panel -> consumed, stays open
    auto b = cc.get_bounds();
    bool handled = cc.handle_pointer_click(b.x + 20, b.y + 20);
    assert(handled);
    assert(cc.is_visible());

    // Click outside panel -> dismissed
    handled = cc.handle_pointer_click(b.x - 50, b.y - 10);
    assert(handled);
    assert(!cc.is_visible());

    // Escape key dismissal
    cc.show();
    assert(cc.is_visible());
    handled = cc.handle_key_down(27); // Escape
    assert(handled);
    assert(!cc.is_visible());

    std::cout << "[PASS] test_input_handling_and_dismissal\n";
}

static void test_quick_action_toggles() {
    SystemStateManager mgr;
    ControlCenterService cc(&mgr);

    assert(mgr.get_appearance().theme == ThemeMode::Dark);
    bool ok = cc.toggle_theme();
    assert(ok);
    assert(mgr.get_appearance().theme == ThemeMode::Light);

    assert(mgr.get_appearance().accent == AccentColor::Blue);
    ok = cc.toggle_accent();
    assert(ok);
    assert(mgr.get_appearance().accent == AccentColor::Purple);

    assert(mgr.get_display().scale == DisplayScale::Scale100);
    ok = cc.cycle_display_scale();
    assert(ok);
    assert(mgr.get_display().scale == DisplayScale::Scale125);

    assert(mgr.get_audio().master_volume == 80);
    ok = cc.step_volume_up(10);
    assert(ok);
    assert(mgr.get_audio().master_volume == 90);
    ok = cc.step_volume_down(20);
    assert(ok);
    assert(mgr.get_audio().master_volume == 70);

    assert(!mgr.get_audio().is_muted);
    ok = cc.toggle_audio_mute();
    assert(ok);
    assert(mgr.get_audio().is_muted);

    assert(!mgr.get_power().low_power_mode);
    ok = cc.toggle_low_power_mode();
    assert(ok);
    assert(mgr.get_power().low_power_mode);

    assert(mgr.get_network().adapter_enabled);
    ok = cc.toggle_network_adapter();
    assert(ok);
    assert(!mgr.get_network().adapter_enabled);

    assert(!cc.is_do_not_disturb());
    ok = cc.toggle_do_not_disturb();
    assert(ok);
    assert(cc.is_do_not_disturb());

    std::cout << "[PASS] test_quick_action_toggles\n";
}

int main() {
    std::cout << "--- Stage 228.11 Control Center Tests ---\n";
    test_visibility_and_geometry();
    test_input_handling_and_dismissal();
    test_quick_action_toggles();
    std::cout << "All Stage 228.11 Control Center Tests PASSED.\n";
    return 0;
}
