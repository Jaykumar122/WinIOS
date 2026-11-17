// ============================================================================
// Stage 214.17: Application Launch and Termination Reliability Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "graphics/virtual_display.h"
#include "input/input_router.h"
#include "kernel/app_lifecycle_manager.h"
#include "runtime/ui/theme.h"

using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::runtime::ui;

namespace {

int g_failures = 0;

#define RELY_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[RELY_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [RELY_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_launch_and_exit_cycles() {
    std::cout << "[Stage 214.17] Verifying Repeated Application Launch and Exit Cycles...\n";

    DisplayConfiguration disp_cfg{};
    disp_cfg.width = 400;
    disp_cfg.height = 600;
    disp_cfg.row_stride = 400 * 4;

    auto display = std::make_shared<VirtualDisplay>(disp_cfg);
    auto comp = std::make_shared<Compositor>(display);
    auto wm = std::make_shared<WindowManager>();
    auto srm = std::make_unique<SurfaceResourceManager>();
    auto router = std::make_unique<InputRouter>(wm);

    // Initial shell window (PID 1)
    std::shared_ptr<DisplaySurface> shell_surf;
    srm->create_surface(1, 400, 600, PixelFormat::B8G8R8A8_UNORM, shell_surf);
    std::uint32_t shell_wid = wm->create_window(1, "Shell", {0, 0, 400, 600}, shell_surf);
    comp->add_surface(shell_surf);
    wm->set_focus(shell_wid);

    const std::size_t initial_windows = wm->window_count();
    RELY_CHECK(initial_windows == 1, "Baseline has 1 window (Shell)");

    AppLifecycleManager lifecycle_mgr(wm.get(), srm.get(), comp.get(), router.get());

    // Execute 10 launch -> terminate cycles for PID 100
    for (int cycle = 1; cycle <= 10; ++cycle) {
        const std::uint32_t app_pid = 100 + static_cast<std::uint32_t>(cycle);
        RELY_CHECK(lifecycle_mgr.launch_app(app_pid, "com.winios.app" + std::to_string(cycle)),
                   "Cycle: App launched in Foreground");

        // App creates surface and window
        std::shared_ptr<DisplaySurface> app_surf;
        srm->create_surface(app_pid, 300, 400, PixelFormat::B8G8R8A8_UNORM, app_surf);
        std::uint32_t wid = wm->create_window(app_pid, "App Window", {50, 50, 300, 400}, app_surf);
        comp->add_surface(app_surf);
        wm->set_focus(wid);

        // Associate with lifecycle manager
        lifecycle_mgr.associate_window(app_pid, wid);
        lifecycle_mgr.associate_surface(app_pid, app_surf->id());
        ThemeManager::instance().track_resource(app_pid, 999);

        RELY_CHECK(wm->window_count() == 2, "Window count is 2 (Shell + App)");
        RELY_CHECK(wm->focused_pid() == app_pid, "App has focus");

        // Terminate app
        RELY_CHECK(lifecycle_mgr.terminate_app(app_pid), "App terminated cleanly");
        RELY_CHECK(wm->window_count() == 1, "Window count restored to 1");
        RELY_CHECK(wm->focused_pid() == 1, "Focus restored to Shell (PID 1)");
        RELY_CHECK(ThemeManager::instance().active_resource_count_for_pid(app_pid) == 0,
                   "All process UI resources released");
    }

    RELY_CHECK(wm->window_count() == initial_windows, "Zero window leaks after 10 cycles");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.17: Launch Reliability Tests      \n";
    std::cout << "====================================================\n";

    test_launch_and_exit_cycles();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL RELIABILITY TESTS PASSED" : "RELIABILITY FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
