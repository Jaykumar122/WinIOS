#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "graphics/virtual_display.h"
#include "renderer/presentation_pacer.h"
#include "input/input_router.h"
#include "kernel/app_lifecycle_manager.h"
#include "security/capability_manager.h"
#include "runtime/system_clipboard.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::renderer;
using namespace winios::input;
using namespace winios::security;
using namespace winios::runtime;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_three_app_desktop_scenario() {
    std::cout << "[Test 1] Three-app desktop integration: Calculator, Notes v2 & Resource Browser...\n";

    auto wm = std::make_shared<WindowManager>();
    auto sm = std::make_shared<SurfaceResourceManager>();
    DisplayConfiguration cfg{};
    cfg.width = 1179;
    cfg.height = 2556;
    cfg.row_stride = 1179 * 4;
    auto display = std::make_shared<VirtualDisplay>(cfg);
    auto comp = std::make_shared<Compositor>(display);
    auto pacer = std::make_shared<PresentationPacer>();
    auto router = std::make_shared<InputRouter>(wm);
    AppLifecycleManager supervisor(wm.get(), sm.get(), comp.get(), router.get());

    auto caps = std::make_shared<CapabilityManager>();
    SystemClipboard clipboard(caps);

    // 1. Launch 3 apps via Lifecycle Supervisor
    // App 1: Calculator (PID 101)
    // App 2: Notes v2 (PID 102)
    // App 3: Resource Browser (PID 103)
    caps->grant_mask(101, Capability::DisplayWindow | Capability::ClipboardWrite | Capability::ClipboardRead);
    caps->grant_mask(102, Capability::DisplayWindow | Capability::ClipboardWrite | Capability::ClipboardRead);
    caps->grant_mask(103, Capability::DisplayWindow | Capability::ClipboardWrite | Capability::ClipboardRead);

    TEST_ASSERT(supervisor.launch_app(101, "com.apple.calculator"));
    TEST_ASSERT(supervisor.send_to_background(101));

    TEST_ASSERT(supervisor.launch_app(102, "com.apple.notes.v2"));
    TEST_ASSERT(supervisor.send_to_background(102));

    TEST_ASSERT(supervisor.launch_app(103, "com.apple.resourcebrowser"));

    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Background);
    TEST_ASSERT(supervisor.get_app_state(102) == AppState::Background);
    TEST_ASSERT(supervisor.get_app_state(103) == AppState::Foreground);

    // 2. Create surfaces & windows
    auto surf_calc = std::make_shared<DisplaySurface>(1, 101, 280, 360);
    surf_calc->set_position(40, 60);
    surf_calc->set_z_order(1);
    surf_calc->fill_rect(101, Rect{0, 0, 280, 360}, 0xFF1C1C1Eu); // Dark calculator
    comp->add_surface(surf_calc);
    uint32_t wid_calc = wm->create_window(101, "Calculator", Rect{40, 60, 280, 360}, surf_calc);

    auto surf_notes = std::make_shared<DisplaySurface>(2, 102, 360, 360);
    surf_notes->set_position(200, 150);
    surf_notes->set_z_order(2);
    surf_notes->fill_rect(102, Rect{0, 0, 360, 360}, 0xFFFAF8F5u); // Parchment notes
    comp->add_surface(surf_notes);
    uint32_t wid_notes = wm->create_window(102, "Notes v2", Rect{200, 150, 360, 360}, surf_notes);

    auto surf_res = std::make_shared<DisplaySurface>(3, 103, 400, 320);
    surf_res->set_position(350, 300);
    surf_res->set_z_order(3);
    surf_res->fill_rect(103, Rect{0, 0, 400, 320}, 0xFFF2F2F7u); // Resource browser
    comp->add_surface(surf_res);
    uint32_t wid_res = wm->create_window(103, "Resource Browser", Rect{350, 300, 400, 320}, surf_res);

    TEST_ASSERT(wm->window_count() == 3);
    TEST_ASSERT(wm->focused_window_id() == wid_res);

    // 3. Composite initial desktop state
    bool comp_ok = comp->composite(true);
    TEST_ASSERT(comp_ok);
    pacer->on_frame_presented(16666000ULL);
    TEST_ASSERT(pacer->diagnostics().total_frames == 1);

    // 4. Focus cycling & hit-testing via input router
    // Click on Calculator window at (100, 100)
    InputEvent click_calc{};
    click_calc.type = InputEventType::MouseDown;
    click_calc.mouse.x = 100;
    click_calc.mouse.y = 100;
    click_calc.mouse.button = MouseButton::Left;
    TEST_ASSERT(router->route_event(click_calc));
    TEST_ASSERT(wm->focused_window_id() == wid_calc);
    TEST_ASSERT(wm->focused_pid() == 101);

    // Click on Notes window at (250, 200)
    InputEvent click_notes{};
    click_notes.type = InputEventType::MouseDown;
    click_notes.mouse.x = 250;
    click_notes.mouse.y = 200;
    click_notes.mouse.button = MouseButton::Left;
    TEST_ASSERT(router->route_event(click_notes));
    TEST_ASSERT(wm->focused_window_id() == wid_notes);
    TEST_ASSERT(wm->focused_pid() == 102);

    // 5. Inter-process Clipboard Data Flow
    // Calculator copies result "42.5" to clipboard
    TEST_ASSERT(clipboard.set_text(101, "42.5"));

    // Notes pastes from clipboard
    std::string notes_pasted;
    TEST_ASSERT(clipboard.get_text(102, notes_pasted));
    TEST_ASSERT(notes_pasted == "42.5");

    // Resource browser copies an asset name
    TEST_ASSERT(clipboard.set_text(103, "app_icon.png"));

    std::string res_pasted;
    TEST_ASSERT(clipboard.get_text(102, res_pasted));
    TEST_ASSERT(res_pasted == "app_icon.png");

    // 6. Window Destruction and Graceful Termination
    TEST_ASSERT(supervisor.terminate_app(101));
    TEST_ASSERT(supervisor.terminate_app(102));
    TEST_ASSERT(supervisor.terminate_app(103));

    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Terminated);
    TEST_ASSERT(supervisor.get_app_state(102) == AppState::Terminated);
    TEST_ASSERT(supervisor.get_app_state(103) == AppState::Terminated);

    std::cout << "[PASS] Three-app desktop integration verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 223.22: THREE-APP DESKTOP TESTS\n";
    std::cout << "========================================\n";

    test_three_app_desktop_scenario();

    std::cout << "\nALL STAGE 223.22 TESTS PASSED (100%).\n";
    return 0;
}
