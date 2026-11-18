#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "apps/sample_apps.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios;
using namespace winios::apps;
using namespace winios::graphics;
using namespace winios::storage;

void test_multi_window_app_lifecycle() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto wm = std::make_shared<WindowManager>();
    auto srm = std::make_shared<SurfaceResourceManager>();
    auto storage = std::make_shared<IsolatedStorage>(vfs, "com.winios.multieditor");

    MultiWindowEditorApp app(4, wm, srm, storage);
    TEST_ASSERT(!app.is_running());

    TEST_ASSERT(app.launch());
    TEST_ASSERT(app.is_running());

    std::uint32_t pwid = app.primary_window_id();
    std::uint32_t twid = app.palette_window_id();
    TEST_ASSERT(pwid != 0);
    TEST_ASSERT(twid != 0);
    TEST_ASSERT(pwid != twid);

    auto pwin = wm->get_window(pwid);
    auto twin = wm->get_window(twid);
    TEST_ASSERT(pwin != nullptr);
    TEST_ASSERT(twin != nullptr);

    // Verify palette is always on top
    TEST_ASSERT(twin->always_on_top == true);
    TEST_ASSERT(pwin->always_on_top == false);

    // Initial state: default Pen tool (tool 0), empty buffer
    TEST_ASSERT(app.current_tool() == 0);
    TEST_ASSERT(app.content().empty());

    // Type text into editor
    app.handle_char('H');
    app.handle_char('e');
    app.handle_char('l');
    app.handle_char('l');
    app.handle_char('o');
    TEST_ASSERT(app.content() == "Hello");

    // Select Tool from Palette: Eraser (tool 1) at y = 100
    TEST_ASSERT(app.handle_palette_tap(50, 100));
    TEST_ASSERT(app.current_tool() == 1);

    // While Eraser is selected, handle_char performs backspace
    app.handle_char('x');
    TEST_ASSERT(app.content() == "Hell");

    // Switch back to Pen (tool 0) at y = 50
    TEST_ASSERT(app.handle_palette_tap(50, 50));
    TEST_ASSERT(app.current_tool() == 0);
    app.handle_char('o');
    app.handle_char('!');
    TEST_ASSERT(app.content() == "Hello!");

    // Save Document using Palette (tool 3) at y = 200
    TEST_ASSERT(app.handle_palette_tap(50, 200));
    TEST_ASSERT(app.current_tool() == 3);

    // Terminate application
    app.terminate();
    TEST_ASSERT(!app.is_running());
    TEST_ASSERT(wm->get_window(pwid) == nullptr);
    TEST_ASSERT(wm->get_window(twid) == nullptr);

    // Relaunch app with same storage -> document should be restored!
    MultiWindowEditorApp app2(4, wm, srm, storage);
    TEST_ASSERT(app2.launch());
    TEST_ASSERT(app2.content() == "Hello!");

    // Test Clear tool (tool 2) at y = 150
    TEST_ASSERT(app2.handle_palette_tap(50, 150));
    TEST_ASSERT(app2.content().empty());

    app2.terminate();
    std::cout << "[PASS] test_multi_window_app_lifecycle\n";
}

void test_multi_window_editor_package() {
    auto pkg = create_multi_window_editor_package();
    TEST_ASSERT(pkg.manifest.bundle_id == "com.winios.multieditor");
    TEST_ASSERT(pkg.manifest.name == "Multi-Window Editor");
    TEST_ASSERT(pkg.manifest.min_os_version == "216.0");
    bool has_bin = false;
    bool has_icon = false;
    for (const auto& f : pkg.manifest.files) {
        if (f.path == "multieditor.bin") has_bin = true;
        if (f.path == "res/icon.png") has_icon = true;
    }
    TEST_ASSERT(has_bin);
    TEST_ASSERT(has_icon);

    std::cout << "[PASS] test_multi_window_editor_package\n";
}

int main() {
    std::cout << "Starting Stage 216.21 Multi-Window App Tests...\n";
    test_multi_window_app_lifecycle();
    test_multi_window_editor_package();
    std::cout << "All Stage 216.21 Tests Passed Successfully!\n";
    return 0;
}
