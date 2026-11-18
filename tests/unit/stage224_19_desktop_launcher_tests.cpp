#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "graphics/desktop_launcher.h"
#include "packaging/app_registry.h"
#include "kernel/app_supervisor.h"
#include "graphics/window_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::graphics;
using namespace winios::packaging;
using namespace winios::kernel;

void test_desktop_launcher_discovery_and_selection() {
    std::cout << "[Test 1] Launcher Application Discovery & Navigation...\n";

    ApplicationRegistry reg;
    AppSupervisor supervisor;
    WindowManager wm;

    AppRegistration app1{};
    app1.bundle_id = "com.winios.calc";
    app1.name = "Calculator";
    app1.version = "1.0.0";
    app1.exec_path = "bin/calc";
    reg.register_app(app1);

    AppRegistration app2{};
    app2.bundle_id = "com.winios.notes";
    app2.name = "Notes v2";
    app2.version = "2.0.0";
    app2.exec_path = "bin/notes";
    reg.register_app(app2);

    DesktopLauncher launcher(&reg, &supervisor, &wm);
    TEST_ASSERT(launcher.item_count() == 2);
    TEST_ASSERT(launcher.items()[0].bundle_id == "com.winios.calc");
    TEST_ASSERT(launcher.items()[1].bundle_id == "com.winios.notes");

    // Selection
    TEST_ASSERT(launcher.selected_index() == 0);
    launcher.select_next();
    TEST_ASSERT(launcher.selected_index() == 1);
    launcher.select_next();
    TEST_ASSERT(launcher.selected_index() == 0); // Wraparound
    launcher.select_previous();
    TEST_ASSERT(launcher.selected_index() == 1);

    std::cout << "[PASS] Discovery and navigation verified.\n";
}

void test_launcher_launch_and_focus() {
    std::cout << "[Test 2] Launch Workflow & Running App Focus Management...\n";

    ApplicationRegistry reg;
    AppSupervisor supervisor;
    WindowManager wm;

    AppRegistration app{};
    app.bundle_id = "com.winios.settings";
    app.name = "Platform Settings";
    app.version = "1.0.0";
    app.exec_path = "bin/settings";
    reg.register_app(app);

    DesktopLauncher launcher(&reg, &supervisor, &wm);

    uint32_t launch_count = 0;
    launcher.set_launch_handler([&](const std::string& bundle_id, uint32_t& out_pid) {
        if (bundle_id == "com.winios.settings") {
            launch_count++;
            out_pid = 70;
            // Simulate window creation by newly launched app
            Rect frame{ 50, 50, 300, 300 };
            wm.create_window(70, "Platform Settings", frame, nullptr);
            return true;
        }
        return false;
    });

    // 1. Initial Launch
    TEST_ASSERT(launcher.launch_bundle("com.winios.settings"));
    TEST_ASSERT(launch_count == 1);
    TEST_ASSERT(supervisor.is_running("com.winios.settings"));
    TEST_ASSERT(wm.window_count() == 1);

    // 2. Second Launch of already running app -> focuses window, DOES NOT relaunch!
    TEST_ASSERT(launcher.launch_bundle("com.winios.settings"));
    TEST_ASSERT(launch_count == 1); // Not relaunched!
    TEST_ASSERT(wm.focused_window_id() > 0);

    // 3. Launch uninstalled app -> fails with error message
    TEST_ASSERT(!launcher.launch_bundle("com.winios.unknown"));
    TEST_ASSERT(!launcher.last_error().empty());

    std::cout << "[PASS] Launch and focus management verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.19: DESKTOP LAUNCHER TESTS\n";
    std::cout << "========================================\n";

    test_desktop_launcher_discovery_and_selection();
    test_launcher_launch_and_focus();

    std::cout << "\nALL STAGE 224.19 TESTS PASSED (100%).\n";
    return 0;
}
